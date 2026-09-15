#include "SeaGameMode.h"

#include "EnemyShipPawn.h"
#include "MerchantShipPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Island.h"
#include "WindSubsystem.h"
#include "OceanSurface.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShipHUD.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "ShipAIController.h"
#include "ShipPawn.h"
#include "TimerManager.h"
#include "WaterBodyActor.h"
#include "GerstnerWaterWaves.h"
#include "WaterBodyComponent.h"
#include "WaterWaves.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "WaterMeshComponent.h"
#include "WaterSubsystem.h"
#include "WaterZoneActor.h"

ASeaGameMode::ASeaGameMode()
{
	DefaultPawnClass = AShipPawn::StaticClass();
	EnemyShipClass = AEnemyShipPawn::StaticClass();
	MerchantShipClass = AMerchantShipPawn::StaticClass();
	HUDClass = AShipHUD::StaticClass();
}

void ASeaGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Seed the world's randomness before anything draws from it.
	//
	// Measured, and it invalidated a method rather than a number: two runs with
	// identical flags and identical code gave 5 rig hits and then 8. With a
	// fixed timestep the two logs are byte-identical for 73 seconds and diverge
	// at the FIRST broadside, which is where the only random draws in the
	// project are - the guns' 1.6 degrees of train scatter and 0.35 of
	// elevation. So the physics and the captain were always repeatable and the
	// gunnery never was, and every before/after comparison this project has
	// made on a single pair of runs was reading scatter as signal.
	//
	// -ShipSeed=N makes a run repeatable. Without it the guns scatter freshly
	// every time, which is what a game wants and what a measurement cannot use.
	int32 Seed = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShipSeed="), Seed))
	{
		FMath::RandInit(Seed);
		FMath::SRandInit(Seed);
		UE_LOG(LogTemp, Display, TEXT("SEALOG random seed=%d (run is repeatable)"), Seed);
	}

	// -WindBearing=N pins the weather. A lee shore is only a lee shore if the
	// wind is where you put it, and the wind normally wanders 22 degrees each
	// way, which is enough to turn one into a weather shore halfway through a
	// measurement.
	float WindBearing = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WindBearing="), WindBearing))
	{
		if (UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>())
		{
			Wind->SetForcedBearing(WindBearing);
			UE_LOG(LogTemp, Display,
				TEXT("SEALOG wind pinned to bearing %.0f (blowing towards it)"),
				WindBearing);
		}
	}

	float WindSpeed = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WindSpeed="), WindSpeed) && WindSpeed > 0.f)
	{
		if (UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>())
		{
			Wind->SetForcedSpeed(WindSpeed);
			UE_LOG(LogTemp, Display, TEXT("SEALOG wind pinned to %.1f m/s"), WindSpeed);
		}
	}

	DumpOceanCollision();
	SetSeaState();
	SetTimeOfDay();
	SpawnOceanSurface();
	// The squadron's size AND its position are read BEFORE the island is
	// placed, because the island has to be checked against every station a
	// hull can be born on. -EnemyX/-EnemyY used to be parsed later, inside
	// SpawnOneEnemy, so the guard compared the island against the DEFAULT
	// spawn and would have waved through an island sitting exactly on the
	// flagged one - in a run whose whole purpose was to put land near it.
	FParse::Value(FCommandLine::Get(), TEXT("EnemyCount="), SquadronSize);
	// ZERO IS NOW ALLOWED. A mission where nobody is hunting you is a thing the
	// objective layer needs, and the clamp made it unsayable. Nothing fires a
	// victory at t=0 for an empty squadron: HandleShipSunk only ever runs from a
	// hull actually sinking, so with none spawned it never runs at all.
	SquadronSize = FMath::Clamp(SquadronSize, 0, 8);
	{
		float OverrideX = 0.f, OverrideY = 0.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("EnemyX="), OverrideX) &&
			FParse::Value(FCommandLine::Get(), TEXT("EnemyY="), OverrideY))
		{
			EnemySpawnLocation.X = OverrideX;
			EnemySpawnLocation.Y = OverrideY;
			UE_LOG(LogTemp, Display, TEXT("SEALOG enemy spawn moved to (%.0f,%.0f)"),
				OverrideX, OverrideY);
		}
	}
	// The convoy's stations, its landfall and the raider's station are all
	// laid here, before the island is, for the same reason the squadron's are.
	ReadConvoyFlags();
	SpawnIslands();
	DumpWorldStaticCensus();
	// The water mesh only decides whether it is enabled inside its own
	// update, which the water subsystem drives from its tick, so asking at
	// BeginPlay always reports "not enabled" whatever the truth is.
	GetWorldTimerManager().SetTimer(WaterDumpTimer, this,
		&ASeaGameMode::DumpWaterRenderingLater, 3.f, false);

	GetWorldTimerManager().SetTimer(EnemySpawnTimer, this,
		&ASeaGameMode::SpawnSquadron, FMath::Max(0.05f, EnemySpawnDelay), false);

	FParse::Value(FCommandLine::Get(), TEXT("ShipQuitAfter="), QuitAfterSeconds);
	if (QuitAfterSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(QuitTimer, this,
			&ASeaGameMode::QuitNow, QuitAfterSeconds, false);
	}

	if (ConvoySize > 0)
	{
		// A quarter of a second BEFORE the squadron, so the merchants are on
		// the water when the raider first looks for a target. Measured the
		// other way round: her first target was the player's idle hull 1.5 km
		// off, for the two seconds until she looked again. The counters look
		// for the raider a second after this and cope with her not being
		// there yet; the raider does not cope with the convoy not being there.
		GetWorldTimerManager().SetTimer(ConvoySpawnTimer, this,
			&ASeaGameMode::SpawnConvoy, FMath::Max(0.05f, EnemySpawnDelay - 0.25f), false);
		FParse::Value(FCommandLine::Get(), TEXT("ConvoyStrikeTest="), ConvoyStrikeTestAt);
		if (ConvoyStrikeTestAt > 0.f)
		{
			GetWorldTimerManager().SetTimer(ConvoyStrikeTestTimer, this,
				&ASeaGameMode::StrikeMerchantForTest, ConvoyStrikeTestAt, false);
		}
	}

	FParse::Value(FCommandLine::Get(), TEXT("ShipSinkTest="), ShipSinkTestAt);
	FParse::Value(FCommandLine::Get(), TEXT("EnemySinkTest="), EnemySinkTestAt);
	FString Side;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShipSinkSide="), Side))
	{
		bSinkTestStarboard = !Side.Equals(TEXT("port"), ESearchCase::IgnoreCase);
	}
	if (ShipSinkTestAt > 0.f)
	{
		GetWorldTimerManager().SetTimer(SinkTestTimer, this,
			&ASeaGameMode::ScuttlePlayerForTest, ShipSinkTestAt, false);
	}
	if (EnemySinkTestAt > 0.f)
	{
		GetWorldTimerManager().SetTimer(EnemySinkTestTimer, this,
			&ASeaGameMode::ScuttleEnemyForTest, EnemySinkTestAt, false);
	}
}

void ASeaGameMode::SetTimeOfDay()
{
	FParse::Value(FCommandLine::Get(), TEXT("Hour="), HourOfDay);
	if (HourOfDay < 0.f)
	{
		// Not asked for. The level keeps the light it was authored with, which
		// is the point: this must not move a single baselined number unless
		// somebody asks for an hour.
		return;
	}
	HourOfDay = FMath::Fmod(FMath::Max(0.f, HourOfDay), 24.f);

	// Where the sun is. A sine between sunrise and sunset for the height, and a
	// sweep from east to west for the bearing - not an ephemeris, but it has
	// the two properties that matter: the light rakes along the water at the
	// ends of the day and comes from a different quarter at each hour.
	const float Day = FMath::Max(1.f, SunsetHour - SunriseHour);
	const float T = (HourOfDay - SunriseHour) / Day;          // 0 at sunrise, 1 at sunset
	const bool bDaylight = (T >= 0.f && T <= 1.f);
	const float ElevDeg = bDaylight
		? NoonElevationDeg * FMath::Sin(PI * T)
		: -12.f;                                              // well under the horizon
	const float AzimuthDeg = 90.f + 180.f * FMath::Clamp(T, -0.2f, 1.2f);

	// How strong, and what colour. Both follow the height, because that is what
	// makes a low sun read as a low sun: less of it, and redder. The floor is
	// not moonlight - it is "enough to sail by", because a black frame is a
	// thing this project has shipped before and had to measure its way out of.
	const float SinElev = FMath::Sin(FMath::DegreesToRadians(FMath::Max(ElevDeg, 0.f)));
	const float Lux = bDaylight
		? FMath::Max(1500.f, 110000.f * FMath::Pow(SinElev, 0.65f))
		: 260.f;
	const float Kelvin = bDaylight
		? FMath::Lerp(2100.f, 5800.f, FMath::Clamp(ElevDeg / 22.f, 0.f, 1.f))
		: 11000.f;
	// The exposure band has to travel with the light or the frame goes white at
	// noon and black at dusk.
	//
	// ANCHORED TO THE POINT THAT WAS MEASURED, not to a formula. The shipped
	// look is 110,000 lux inside a band of 12.5 to 16 EV, arrived at by sweeping
	// and looking; every other hour is that band moved by however many stops the
	// light has moved. A band computed from first principles instead put dusk
	// two and a half stops too high and turned a correctly coloured sunset into
	// a silhouette - the arithmetic was fine and the anchor was invented.
	const float Stops = FMath::Log2(FMath::Max(Lux, 1.f) / 110000.f);
	const float EvMin = 12.5f + Stops, EvMax = 16.f + Stops;

	int32 Suns = 0, Skies = 0, Volumes = 0;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		ADirectionalLight* Sun = *It;
		Sun->SetActorRotation(FRotator(-ElevDeg, AzimuthDeg, 0.f));
		if (UDirectionalLightComponent* Comp =
			Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Comp->SetIntensity(Lux);
			Comp->SetTemperature(Kelvin);
			Comp->SetUseTemperature(true);
			++Suns;
		}
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		if (USkyLightComponent* Comp = It->GetLightComponent())
		{
			// The sky itself is what lights the shadows, and at dusk it is
			// nearly all of the light there is.
			Comp->SetIntensity(bDaylight ? 1.f : 2.6f);
			Comp->RecaptureSky();
			++Skies;
		}
	}
	for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
	{
		It->Settings.bOverride_AutoExposureMinBrightness = true;
		It->Settings.bOverride_AutoExposureMaxBrightness = true;
		It->Settings.AutoExposureMinBrightness = EvMin;
		It->Settings.AutoExposureMaxBrightness = EvMax;
		++Volumes;
	}

	// Counted, because "the hour did nothing" and "the hour is wrong" look the
	// same in a capture, and a level with no directional light would give the
	// first while every number below still read perfectly.
	UE_LOG(LogTemp, Display,
		TEXT("SKYLOG hour=%.1f elev=%.1f azim=%.0f lux=%.0f K=%.0f ev=[%.1f,%.1f] "
			 "suns=%d skies=%d volumes=%d"),
		HourOfDay, ElevDeg, AzimuthDeg, Lux, Kelvin, EvMin, EvMax,
		Suns, Skies, Volumes);
}

void ASeaGameMode::SetSeaState()
{
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		const float Before = It->GetWaterWaves() ? It->GetWaterWaves()->GetMaxWaveHeight() : 0.f;

		UGerstnerWaterWaves* Waves = NewObject<UGerstnerWaterWaves>(*It);
		UGerstnerWaterWaveGeneratorSimple* Generator =
			NewObject<UGerstnerWaterWaveGeneratorSimple>(Waves);
		Generator->NumWaves = SwellWaveCount;
		Generator->MinWavelength = 900.f;
		Generator->MaxWavelength = 7000.f;
		// THE WIND. This used to be two constants and a compass bearing of 25
		// degrees, so the sea was the same sea in a flat calm and a gale, and the
		// swell rolled from due north-north-east whatever the wind was doing. A
		// wind sweep changed the sailing and changed nothing about the water -
		// which meant every "no change across the wind range" reading was not a
		// fix holding at both extremes, it was an input that never moved.
		float WindMS = 11.f, WindDeg = 25.f;
		if (const UWindSubsystem* W = GetWorld()->GetSubsystem<UWindSubsystem>())
		{
			WindMS = W->GetWindSpeedMS();
			WindDeg = W->GetWindBearingDeg();
		}
		// One knob, against the eleven metres a second the swell was authored
		// for. Floored so a calm still has a swell rolling under her - the ocean
		// does not go flat because the wind dropped this morning - and capped
		// well short of the five-metre sea the map came with, which is heavy
		// weather for a thirty-seven metre hull.
		const float Gain = FMath::Clamp(WindMS / 11.f, 0.45f, 1.8f);
		Generator->MinAmplitude = SwellMinAmplitudeCm * Gain;
		Generator->MaxAmplitude = SwellMaxAmplitudeCm * Gain;
		Generator->WindAngleDeg = WindDeg;
		// The spread was 400 degrees. The engine rotates every wave after the
		// first by FRandRange(-spread, +spread), so at 400 the set was uniform
		// over the whole circle: a sea with no direction at all, which is why
		// setting the bearing alone would have changed nothing visible.
		Generator->DirectionAngularSpreadDeg = SwellSpreadDeg;
		Waves->GerstnerWaveGenerator = Generator;
		// The waves object computes its wave set ONCE, in its constructor,
		// with the default generator. Assigning a generator afterwards changes
		// nothing at all until this is called: the log said "max height 508"
		// whatever amplitudes were set above.
		Waves->RecomputeWaves(false);
		It->SetWaterWaves(Waves);

		// The INPUT in the line, not just the output: a constant that is printed
		// beside nothing reads as a confirmation every time it is looked at.
		UE_LOG(LogTemp, Display,
			TEXT("SEALOG sea state on %s: %d waves, max height %.0f cm (was %.0f) "
				 "at wind %.1f m/s bearing %.0f (gain %.2f, spread %.0f)"),
			*It->GetName(), Generator->NumWaves, Waves->GetMaxWaveHeight(), Before,
			WindMS, WindDeg, Gain, SwellSpreadDeg);
	}
}

void ASeaGameMode::SpawnOceanSurface()
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Sea = GetWorld()->SpawnActor<AOceanSurface>(AOceanSurface::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UE_LOG(LogTemp, Display, TEXT("SEALOG ocean surface=%s"),
		Sea ? *Sea->GetName() : TEXT("FAILED"));
}

void ASeaGameMode::SpawnIslands()
{
	int32 Count = 0;
	FParse::Value(FCommandLine::Get(), TEXT("Islands="), Count);
	if (Count <= 0)
	{
		return;
	}
	Count = FMath::Clamp(Count, 1, 8);

	// Halfway between where the player starts and where the squadron stands
	// out, so the land is in the way of the fight rather than decorating the
	// horizon where nothing would ever meet it.
	float X = EnemySpawnLocation.X * 0.5f;
	float Y = EnemySpawnLocation.Y * 0.5f;
	float Radius = AIsland::AuthoredShoreCm;
	float Spread = 0.f;
	FParse::Value(FCommandLine::Get(), TEXT("IsleX="), X);
	FParse::Value(FCommandLine::Get(), TEXT("IsleY="), Y);
	FParse::Value(FCommandLine::Get(), TEXT("IsleRadius="), Radius);
	FParse::Value(FCommandLine::Get(), TEXT("IsleSpread="), Spread);

	const float ShoalOuter = Radius * AIsland::AuthoredShoalOuterCm
		/ AIsland::AuthoredShoreCm;
	if (Spread <= 0.f)
	{
		// Close enough that the banks nearly touch, so a line of them is a
		// COAST rather than a scattering of rocks: a ship driven down on it
		// has to weather one end, which is the whole point of a lee shore.
		Spread = ShoalOuter * 2.2f;
	}

	// Laid ACROSS the wind, so a line of islands is a shore to leeward rather
	// than a channel. Without a wind to lay them against, north-south.
	FVector Along(0.f, 1.f, 0.f);
	if (const UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>())
	{
		const FVector Down = Wind->GetWindDirection().GetSafeNormal2D();
		if (!Down.IsNearlyZero())
		{
			Along = FVector::CrossProduct(FVector::UpVector, Down);
		}
	}

	// Every point a hull can be born on. An island over one of them is not a
	// scenario, it is a broken run: a hull born inside the bank is thrown
	// clear at thirty metres a second, and IsSpawnClear has never heard of
	// land. The squadron forms abeam at +/- spacing, so the stations are
	// checked one by one and not just their middle.
	TArray<FVector> Births;
	Births.Add(FVector::ZeroVector);
	for (int32 i = 0; i < SquadronSize; ++i)
	{
		const float Offset = (i - (SquadronSize - 1) * 0.5f) * SquadronSpacingCm;
		Births.Add(EnemySpawnLocation + FVector(0.f, Offset, 0.f));
	}
	// The convoy's stations, and the roadstead she is running for: an island
	// on the landfall is not a scenario either, it is three merchants driven
	// ashore by their own orders.
	for (const FVector& Station : ConvoyStations)
	{
		Births.Add(Station);
	}
	if (ConvoySize > 0)
	{
		Births.Add(Landfall);
	}

	int32 Built = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Step = (i - (Count - 1) * 0.5f) * Spread;
		const FVector Where(X + Along.X * Step, Y + Along.Y * Step, 0.f);

		const float Bad = ShoalOuter + RespawnClearRadiusCm;
		bool bRefused = false;
		for (const FVector& Birth : Births)
		{
			if (FVector::Dist2D(Birth, Where) < Bad)
			{
				UE_LOG(LogTemp, Error,
					TEXT("SEALOG island %d REFUSED: it covers a spawn at (%.0f,%.0f); "
						 "its bank plus clearance reaches %.0f cm. Move it with -IsleX/-IsleY."),
					i, Birth.X, Birth.Y, Bad);
				bRefused = true;
				break;
			}
		}
		if (bRefused)
		{
			continue;
		}

		FTransform At(FRotator::ZeroRotator, Where);
		AIsland* Isle = GetWorld()->SpawnActorDeferred<AIsland>(AIsland::StaticClass(),
			At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Isle)
		{
			UE_LOG(LogTemp, Warning, TEXT("SEALOG island %d FAILED to spawn"), i);
			continue;
		}
		// Deferred, because for an actor spawned once the world is running
		// BeginPlay runs INSIDE SpawnActor: setting the size on the returned
		// pointer is already too late.
		Isle->ShoreRadiusCm = Radius;
		Isle->FinishSpawning(At);
		++Built;
		UE_LOG(LogTemp, Display,
			TEXT("SEALOG island %d=%s at (%.0f,%.0f) shore=%.0f"),
			i, *Isle->GetName(), Where.X, Where.Y, Radius);
	}

	UE_LOG(LogTemp, Display,
		TEXT("SEALOG islands built=%d of %d asked, spread=%.0f along (%.2f,%.2f)"),
		Built, Count, Spread, Along.X, Along.Y);
}

void ASeaGameMode::DumpWorldStaticCensus() const
{
	int32 Count = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TArray<UPrimitiveComponent*> Comps;
		It->GetComponents<UPrimitiveComponent>(Comps);
		for (const UPrimitiveComponent* Comp : Comps)
		{
			if (!Comp || Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				|| Comp->GetCollisionObjectType() != ECC_WorldStatic)
			{
				continue;
			}
			++Count;
			const FBoxSphereBounds B = Comp->Bounds;
			// enabled 1=QueryOnly 3=QueryAndPhysics; resp 0=Ignore 1=Overlap
			// 2=Block. The ball's sweep is a QUERY, so "enabled" is the field
			// that decides whether this body can answer it at all.
			UE_LOG(LogTemp, Display,
				TEXT("SEALOG worldstatic %s.%s enabled=%d centre=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f) respPawn=%d"),
				*It->GetName(), *Comp->GetName(), (int32)Comp->GetCollisionEnabled(),
				B.Origin.X, B.Origin.Y, B.Origin.Z,
				B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z,
				(int32)Comp->GetCollisionResponseToChannel(ECC_Pawn));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("SEALOG worldstatic count=%d"), Count);
}

void ASeaGameMode::DumpOceanCollision() const
{
	// Legend: enabled 1=QueryOnly 3=QueryAndPhysics; objType 0=WorldStatic
	// 1=WorldDynamic 3=Pawn 5=PhysicsBody; resp 0=Ignore 1=Overlap 2=Block.
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		UWaterBodyComponent* Component = It->GetWaterBodyComponent();
		if (!Component)
		{
			continue;
		}
		for (UPrimitiveComponent* Prim : Component->GetCollisionComponents())
		{
			if (!Prim)
			{
				continue;
			}
			const FBoxSphereBounds B = Prim->Bounds;
			UE_LOG(LogTemp, Display,
				TEXT("SEALOG ocean collision %s class=%s enabled=%d objType=%d respPawn=%d respPhys=%d top=%.0f bottom=%.0f halfX=%.0f"),
				*Prim->GetName(), *Prim->GetClass()->GetName(),
				(int32)Prim->GetCollisionEnabled(), (int32)Prim->GetCollisionObjectType(),
				(int32)Prim->GetCollisionResponseToChannel(ECC_Pawn),
				(int32)Prim->GetCollisionResponseToChannel(ECC_PhysicsBody),
				B.Origin.Z + B.BoxExtent.Z, B.Origin.Z - B.BoxExtent.Z, B.BoxExtent.X);
		}
	}
}

bool ASeaGameMode::IsSpawnClear(const FVector& Where, const AShipPawn* Ignore) const
{
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		const AShipPawn* Other = *It;
		if (Other == Ignore || !IsValid(Other))
		{
			continue;
		}
		if (FVector::Dist2D(Other->GetActorLocation(), Where) > RespawnClearRadiusCm)
		{
			continue;
		}
		// The HULL's box, not the actor's bounding box. The actor's box now
		// includes the query-only rig volumes, which reach 25 m into the air:
		// a wreck's masts do not obstruct a spawn, and using them would have
		// meant waiting for the hull to reach 35 m down, which never happens
		// before it is destroyed.
		const float TopZ = Other->GetHullTopZ();
		if (TopZ < RespawnClearDepthCm)
		{
			continue;
		}
		UE_LOG(LogTemp, Display, TEXT("SEALOG spawn blocked by %s top=%.0f dist=%.0f"),
			*Other->GetName(), TopZ,
			FVector::Dist2D(Other->GetActorLocation(), Where));
		return false;
	}
	return true;
}

void ASeaGameMode::DumpWaterRendering() const
{
	// Why the sea does not draw. Every gate between "there is an ocean in the
	// level" and "there are triangles on screen", printed once.
	UE_LOG(LogTemp, Display, TEXT("SEALOG water canEverRender=%d meshCVar=%d renderCVar=%d"),
		FApp::CanEverRender() ? 1 : 0,
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.Water.WaterMesh.Enabled"))
			? IConsoleManager::Get().FindConsoleVariable(TEXT("r.Water.WaterMesh.Enabled"))->GetInt() : -1,
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.Water.WaterMesh.EnableRendering"))
			? IConsoleManager::Get().FindConsoleVariable(TEXT("r.Water.WaterMesh.EnableRendering"))->GetInt() : -1);

	for (TActorIterator<AWaterZone> It(GetWorld()); It; ++It)
	{
		AWaterZone* Zone = *It;
		UWaterMeshComponent* Mesh = Zone->GetWaterMeshComponent();
		int32 Bodies = 0;
		Zone->ForEachWaterBodyComponent([&Bodies](UWaterBodyComponent*) { ++Bodies; return true; });
		UE_LOG(LogTemp, Display,
			TEXT("SEALOG water zone %s extent=%s bodies=%d mesh=%s enabled=%d visible=%d registered=%d materials=%d bounds=%.0f"),
			*Zone->GetName(), *Zone->GetZoneExtent().ToString(), Bodies,
			Mesh ? *Mesh->GetName() : TEXT("NONE"),
			Mesh ? (Mesh->IsEnabled() ? 1 : 0) : -1,
			Mesh ? (Mesh->IsVisible() ? 1 : 0) : -1,
			Mesh ? (Mesh->IsRegistered() ? 1 : 0) : -1,
			Mesh ? Mesh->GetUsedMaterialsSet().Num() : -1,
			Mesh ? Mesh->Bounds.BoxExtent.X : -1.f);
	}

	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		UWaterBodyComponent* Body = It->GetWaterBodyComponent();
		if (!Body)
		{
			continue;
		}
		const AWaterZone* Zone = Body->GetWaterZone();
		UE_LOG(LogTemp, Display,
			TEXT("SEALOG water body %s zone=%s waves=%d maxWaveHeight=%.0f material=%s shouldRender=%d generatesTile=%d"),
			*It->GetName(), Zone ? *Zone->GetName() : TEXT("NONE"),
			Body->HasWaves() ? 1 : 0, Body->GetMaxWaveHeight(),
			Body->GetWaterMaterial() ? *Body->GetWaterMaterial()->GetName() : TEXT("NONE"),
			Body->ShouldRender() ? 1 : 0, Body->ShouldGenerateWaterMeshTile() ? 1 : 0);

		// The quad tree is fed from the water body's INFO MESH, a static mesh
		// baked from the spline. If that is missing, the zone has nothing to
		// draw however healthy every other flag looks. The component type is
		// private to the plugin, so look at every static mesh on the actor.
		TArray<UStaticMeshComponent*> Meshes;
		It->GetComponents<UStaticMeshComponent>(Meshes);
		for (UStaticMeshComponent* SM : Meshes)
		{
			UStaticMesh* Asset = SM ? SM->GetStaticMesh() : nullptr;
			UE_LOG(LogTemp, Display,
				TEXT("SEALOG water mesh comp %s class=%s asset=%s tris=%d visible=%d registered=%d"),
				*SM->GetName(), *SM->GetClass()->GetName(),
				Asset ? *Asset->GetName() : TEXT("NONE"),
				(Asset && Asset->GetRenderData()) ? Asset->GetNumTriangles(0) : -1,
				SM->IsVisible() ? 1 : 0, SM->IsRegistered() ? 1 : 0);
		}
	}
}

void ASeaGameMode::DumpWaterRenderingLater()
{
	DumpWaterRendering();
}

void ASeaGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);

	// Called by the engine for the first spawn and for every respawn.
	if (AShipPawn* Ship = Cast<AShipPawn>(PlayerPawn))
	{
		PlayerShip = Ship;
		BindShip(Ship);
		if (!bPlayerSpawnRecorded)
		{
			PlayerSpawnTransform = Ship->GetActorTransform();
			bPlayerSpawnRecorded = true;
		}
		UE_LOG(LogTemp, Display, TEXT("SEALOG player ship=%s bound t=%.1f"),
			*Ship->GetName(), GetWorld()->GetTimeSeconds());
	}
}

void ASeaGameMode::BindShip(AShipPawn* Ship)
{
	Ship->OnShipSunk.AddUObject(this, &ASeaGameMode::HandleShipSunk);
	Ship->OnShipWrecked.AddUObject(this, &ASeaGameMode::HandleShipWrecked);
}

TArray<AShipPawn*> ASeaGameMode::GetOrderOfBattle() const
{
	TArray<AShipPawn*> Order;
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Squadron)
	{
		AShipPawn* Ship = Ptr.Get();
		if (IsValid(Ship) && !Ship->IsSunk())
		{
			Order.Add(Ship);
		}
	}
	return Order;
}

int32 ASeaGameMode::CountEnemiesAfloat() const
{
	int32 Afloat = 0;
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Squadron)
	{
		const AShipPawn* Ship = Ptr.Get();
		if (IsValid(Ship) && !Ship->IsSunk())
		{
			++Afloat;
		}
	}
	return Afloat;
}

AShipPawn* ASeaGameMode::SpawnOneEnemy(const FVector& Where, const FRotator& Heading, int32 Station)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APawn* Enemy = GetWorld()->SpawnActor<APawn>(EnemyShipClass, Where, Heading, Params);
	UE_LOG(LogTemp, Display, TEXT("SEALOG enemy spawned=%s station=%d at %s"),
		Enemy ? *Enemy->GetName() : TEXT("FAILED"), Station, *Where.ToCompactString());

	if (Enemy && !Enemy->GetController())
	{
		Enemy->SpawnDefaultController();
	}

	AShipPawn* Ship = Cast<AShipPawn>(Enemy);
	if (Ship)
	{
		Squadron.Add(Ship);
		BindShip(Ship);
	}
	return Ship;
}

void ASeaGameMode::SpawnSquadron()
{
	if (!EnemyShipClass || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("SEALOG no enemy class set"));
		return;
	}

	// Optional override so the spawn distance can be varied from the command
	// line without a rebuild. Two separate values: a comma inside one token
	// gets eaten by the command line parser.
	float OverrideX = 0.f, OverrideY = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("EnemyX="), OverrideX) &&
		FParse::Value(FCommandLine::Get(), TEXT("EnemyY="), OverrideY))
	{
		EnemySpawnLocation = FVector(OverrideX, OverrideY, 0.f);
		UE_LOG(LogTemp, Display, TEXT("SEALOG spawn override to %.0f,%.0f"),
			OverrideX, OverrideY);
	}

	float OverrideYaw = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("EnemyYaw="), OverrideYaw))
	{
		EnemySpawnYaw = OverrideYaw;
	}

	// A hull born outside the ocean's collision box has no water under it.
	const FVector Clamped(
		FMath::Clamp(EnemySpawnLocation.X, -OceanHalfExtentCm, OceanHalfExtentCm),
		FMath::Clamp(EnemySpawnLocation.Y, -OceanHalfExtentCm, OceanHalfExtentCm),
		0.f);
	if (!Clamped.Equals(EnemySpawnLocation))
	{
		UE_LOG(LogTemp, Display, TEXT("SEALOG spawn clamped to %s"), *Clamped.ToCompactString());
		EnemySpawnLocation = Clamped;
	}

	// Formed abeam of one another, the way a squadron stands out before it
	// bears down. Only hulls that are gone are dropped from the list: a ship
	// still sinking is still in the way.
	Squadron.RemoveAll([](const TWeakObjectPtr<AShipPawn>& Ptr) { return !Ptr.IsValid(); });

	const FRotator Heading(0.f, EnemySpawnYaw, 0.f);
	const FVector Abeam = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
	for (int32 i = 0; i < SquadronSize; ++i)
	{
		const float Offset = (i - (SquadronSize - 1) * 0.5f) * SquadronSpacingCm;
		SpawnOneEnemy(EnemySpawnLocation + Abeam * Offset, Heading, i);
	}
	UE_LOG(LogTemp, Display, TEXT("SEALOG squadron of %d stood out"), SquadronSize);
}

void ASeaGameMode::HandleShipSunk(AShipPawn* Ship, AActor* Causer)
{
	// This can run inside a physics hit callback (ball -> damage -> sunk), so
	// nothing here spawns, destroys or possesses: it logs, counts and arms
	// timers. The tracked pointer comes first because IsPlayerControlled()
	// flips to false the moment a pawn is unpossessed.
	const bool bPlayer = (Ship == PlayerShip.Get()) || Ship->IsPlayerControlled();
	const float Now = GetWorld()->GetTimeSeconds();
	const FString By = Causer ? Causer->GetName() : FString(TEXT("none"));

	if (Ship->GetAllegiance() == EShipAllegiance::Merchant)
	{
		// Cargo on the bottom is cargo nobody gets. Not a defeat and not a
		// victory, and the squadron's accounting below must not see her: with
		// -EnemyCount=0 it would have read "no enemies left" and declared one.
		if (Ship->HasStruck())
		{
			// Already counted as stopped; sinking her afterwards changes the
			// prize, not the tally. Said out loud because it is a thing a
			// raider does by mistake.
			UE_LOG(LogTemp, Display,
				TEXT("CONVOYLOG %s sunk AFTER striking by=%s t=%.1f"),
				*Ship->GetName(), *By, Now);
			return;
		}
		++ConvoySunk;
		UE_LOG(LogTemp, Display,
			TEXT("CONVOYLOG %s sunk by=%s t=%.1f (cargo lost) sunk=%d"),
			*Ship->GetName(), *By, Now, ConvoySunk);
		if (ConvoyThrough + ConvoySunk > ConvoySize - ConvoyNeed)
		{
			FinishMission(TEXT("THROUGH"));
		}
		return;
	}

	if (bPlayer)
	{
		++Defeats;
		UE_LOG(LogTemp, Display, TEXT("SEALOG DEFEAT ship=%s by=%s t=%.1f"),
			*Ship->GetName(), *By, Now);
		GetWorldTimerManager().SetTimer(PlayerRespawnTimer, this,
			&ASeaGameMode::TryRespawnPlayer, PlayerRespawnDelay, false);
	}
	else
	{
		// One enemy down is not a victory while her consorts are still firing.
		// The count runs from her sinking, so she is already excluded.
		const int32 Left = CountEnemiesAfloat();
		UE_LOG(LogTemp, Display, TEXT("SEALOG enemy struck ship=%s by=%s t=%.1f left=%d"),
			*Ship->GetName(), *By, Now, Left);
		if (Left == 0)
		{
			++Victories;
			UE_LOG(LogTemp, Display, TEXT("SEALOG VICTORY squadron beaten t=%.1f"), Now);
			GetWorldTimerManager().SetTimer(EnemyRespawnTimer, this,
				&ASeaGameMode::TryRespawnEnemy, EnemyRespawnDelay, false);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("SEALOG score victories=%d defeats=%d"),
		Victories, Defeats);
}

void ASeaGameMode::HandleShipWrecked(AShipPawn* Ship)
{
	UE_LOG(LogTemp, Display, TEXT("SEALOG wreck cleared ship=%s t=%.1f"),
		*Ship->GetName(), GetWorld()->GetTimeSeconds());
	if (PlayerShip.Get() == Ship)
	{
		PlayerShip.Reset();
		// The camera rode the wreck down; now it has nothing to look at, so
		// do not leave the player watching an empty sea for the remainder of
		// a fixed delay.
		if (GetWorldTimerManager().IsTimerActive(PlayerRespawnTimer) &&
			GetWorldTimerManager().GetTimerRemaining(PlayerRespawnTimer) > WreckGoneRespawnSeconds)
		{
			GetWorldTimerManager().SetTimer(PlayerRespawnTimer, this,
				&ASeaGameMode::TryRespawnPlayer, WreckGoneRespawnSeconds, false);
			UE_LOG(LogTemp, Display, TEXT("SEALOG respawn brought forward to %.0fs"),
				WreckGoneRespawnSeconds);
		}
	}
	Squadron.RemoveAll([Ship](const TWeakObjectPtr<AShipPawn>& Ptr)
		{ return !Ptr.IsValid() || Ptr.Get() == Ship; });
}

void ASeaGameMode::TryRespawnPlayer()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Something already gave them a ship back (the engine restarts an
	// inactive player controller on its own in some paths): nothing to do.
	AShipPawn* Wreck = Cast<AShipPawn>(PC->GetPawn());
	if (IsValid(Wreck) && !Wreck->IsSinking())
	{
		UE_LOG(LogTemp, Display, TEXT("SEALOG respawn skipped, %s is already afloat"),
			*Wreck->GetName());
		return;
	}

	// Do not put a new hull on top of a wreck still at the surface, or on top
	// of the enemy that is circling the spot.
	if (!IsSpawnClear(PlayerSpawnTransform.GetLocation(), nullptr))
	{
		// Bounded, for the same reason: a hull parked on the player's start
		// would otherwise leave them without a ship for the rest of the run.
		if (++PlayerRespawnAttempts <= MaxRespawnAttempts)
		{
			GetWorldTimerManager().SetTimer(PlayerRespawnTimer, this,
				&ASeaGameMode::TryRespawnPlayer, 1.f, false);
			return;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("SEALOG player respawn gave up after %d tries, spawning anyway"),
			PlayerRespawnAttempts);
	}
	PlayerRespawnAttempts = 0;

	// RestartPlayerAtTransform spawns nothing while the controller holds a
	// pawn. The wreck keeps sinking on its own.
	if (PC->GetPawn())
	{
		PC->UnPossess();
	}
	if (bPlayerSpawnRecorded)
	{
		RestartPlayerAtTransform(PC, PlayerSpawnTransform);
	}
	else
	{
		RestartPlayer(PC);
	}
	UE_LOG(LogTemp, Display, TEXT("SEALOG respawn player=%s at %s t=%.1f"),
		PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("FAILED"),
		*PlayerSpawnTransform.GetLocation().ToCompactString(),
		GetWorld()->GetTimeSeconds());
}

void ASeaGameMode::TryRespawnEnemy()
{
	if (CountEnemiesAfloat() > 0)
	{
		// Somebody is still up: nothing to replace.
		return;
	}
	// EVERY station, not just the middle one. The stations are laid out
	// SquadronSpacingCm apart and the clearance radius is a fraction of that,
	// so checking the centre could not see the others at all.
	const FRotator Heading(0.f, EnemySpawnYaw, 0.f);
	const FVector Abeam = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
	bool bClear = true;
	for (int32 i = 0; i < SquadronSize && bClear; ++i)
	{
		const float Offset = (i - (SquadronSize - 1) * 0.5f) * SquadronSpacingCm;
		bClear = IsSpawnClear(EnemySpawnLocation + Abeam * Offset, nullptr);
	}
	if (!bClear)
	{
		// Bounded: an unbounded retry would hold the timer for the rest of the
		// session behind a wreck parked on a station, and say nothing at all
		// after the first line.
		if (++EnemyRespawnAttempts <= MaxRespawnAttempts)
		{
			GetWorldTimerManager().SetTimer(EnemyRespawnTimer, this,
				&ASeaGameMode::TryRespawnEnemy, 1.f, false);
			return;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("SEALOG enemy respawn gave up after %d tries, standing out anyway"),
			EnemyRespawnAttempts);
	}
	EnemyRespawnAttempts = 0;
	SpawnSquadron();
}

void ASeaGameMode::ReadConvoyFlags()
{
	FParse::Value(FCommandLine::Get(), TEXT("Convoy="), ConvoySize);
	ConvoySize = FMath::Clamp(ConvoySize, 0, 6);
	if (ConvoySize <= 0)
	{
		return;
	}
	FParse::Value(FCommandLine::Get(), TEXT("ConvoyNeed="), ConvoyNeed);
	if (ConvoyNeed <= 0)
	{
		ConvoyNeed = (ConvoySize + 1) / 2;
	}
	ConvoyNeed = FMath::Min(ConvoyNeed, ConvoySize);
	FParse::Value(FCommandLine::Get(), TEXT("ConvoyX="), ConvoyStart.X);
	FParse::Value(FCommandLine::Get(), TEXT("ConvoyY="), ConvoyStart.Y);
	FParse::Value(FCommandLine::Get(), TEXT("ConvoyWindAngle="), ConvoyWindAngleDeg);
	FParse::Value(FCommandLine::Get(), TEXT("ConvoyRangeM="), ConvoyRangeM);
	FParse::Value(FCommandLine::Get(), TEXT("RaiderOffingM="), RaiderOffingM);
	FParse::Value(FCommandLine::Get(), TEXT("ConvoyCargo="), ConvoyCargo);
	ConvoyCargo = FMath::Max(0, ConvoyCargo);
	FParse::Value(FCommandLine::Get(), TEXT("PrizeCrew="), PrizeCrewHands);
	FParse::Value(FCommandLine::Get(), TEXT("PrizeRangeM="), PrizeRangeM);
	FParse::Value(FCommandLine::Get(), TEXT("PrizeBoatSeconds="), PrizeBoatSeconds);

	// The wind as it stands at BeginPlay: pinned by -WindBearing= a moment
	// ago, or the subsystem's base bearing. The course is laid ONCE, off this
	// wind; a wind that wanders afterwards is the merchant's problem, as it
	// would be.
	const UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>();
	const float WindTo = Wind ? Wind->GetWindBearingDeg() : 0.f;
	const float WindFrom = FMath::UnwindDegrees(WindTo + 180.f);
	ConvoyCourseYaw = FMath::UnwindDegrees(WindFrom + ConvoyWindAngleDeg);

	ConvoyStart.X = FMath::Clamp(ConvoyStart.X, -OceanHalfExtentCm, OceanHalfExtentCm);
	ConvoyStart.Y = FMath::Clamp(ConvoyStart.Y, -OceanHalfExtentCm, OceanHalfExtentCm);
	ConvoyStart.Z = 0.f;

	// The landfall, INSIDE the ocean box: a roadstead with no water in it is
	// a merchant sailing off the edge of the sea. If the clamp moves it, the
	// course is re-laid from the start to where it ended up, so the log's
	// course and range are the ones actually sailed.
	const FVector Course = FRotator(0.f, ConvoyCourseYaw, 0.f).Vector();
	const FVector Asked = ConvoyStart + Course * ConvoyRangeM * 100.f;
	Landfall = FVector(
		FMath::Clamp(Asked.X, -OceanHalfExtentCm, OceanHalfExtentCm),
		FMath::Clamp(Asked.Y, -OceanHalfExtentCm, OceanHalfExtentCm), 0.f);
	if (!Landfall.Equals(Asked))
	{
		ConvoyCourseYaw = (Landfall - ConvoyStart).Rotation().Yaw;
		ConvoyRangeM = FVector::Dist2D(Landfall, ConvoyStart) * 0.01f;
		UE_LOG(LogTemp, Warning,
			TEXT("CONVOYLOG landfall clamped into the ocean box: course re-laid to %.0f, %.0f m"),
			ConvoyCourseYaw, ConvoyRangeM);
	}

	const FVector Abeam = FRotationMatrix(FRotator(0.f, ConvoyCourseYaw, 0.f)).GetUnitAxis(EAxis::Y);
	for (int32 i = 0; i < ConvoySize; ++i)
	{
		const float Offset = (i - (ConvoySize - 1) * 0.5f) * ConvoySpacingCm;
		ConvoyStations.Add(ConvoyStart + Abeam * Offset);
	}
	UE_LOG(LogTemp, Display,
		TEXT("CONVOYLOG convoy of %d laid from (%.0f,%.0f) course %.0f (%.0f off the wind) to landfall (%.0f,%.0f) %.0f m, need %d"),
		ConvoySize, ConvoyStart.X, ConvoyStart.Y, ConvoyCourseYaw,
		FMath::Abs(FMath::FindDeltaAngleDegrees(ConvoyCourseYaw, WindFrom)),
		Landfall.X, Landfall.Y, ConvoyRangeM, ConvoyNeed);

	// -RaiderSide=weather|lee moves the SQUADRON's spawn to that side of the
	// convoy, along the wind. The raider in a measured run is an ordinary
	// enemy hull with the ordinary captain, who hunts the nearest hostile
	// hull - and the merchants are hostile to her. The player's hull stays
	// where it is, player-controlled and idle, as lee_shore leaves it.
	FString Side;
	if (FParse::Value(FCommandLine::Get(), TEXT("RaiderSide="), Side))
	{
		RaiderSide = Side.ToLower();
		const bool bLee = RaiderSide == TEXT("lee");
		if (!bLee)
		{
			RaiderSide = TEXT("weather");
		}
		const FVector Downwind = FRotator(0.f, WindTo, 0.f).Vector();
		const FVector Station = ConvoyStart + Downwind * (bLee ? 1.f : -1.f) * RaiderOffingM * 100.f;
		EnemySpawnLocation = FVector(
			FMath::Clamp(Station.X, -OceanHalfExtentCm, OceanHalfExtentCm),
			FMath::Clamp(Station.Y, -OceanHalfExtentCm, OceanHalfExtentCm), 0.f);
		EnemySpawnYaw = (ConvoyStart - EnemySpawnLocation).Rotation().Yaw;
		UE_LOG(LogTemp, Display,
			TEXT("CONVOYLOG raider placed to %s of the convoy, %.0f m off, at (%.0f,%.0f) heading %.0f"),
			*RaiderSide, RaiderOffingM, EnemySpawnLocation.X, EnemySpawnLocation.Y,
			EnemySpawnYaw);
	}
}

void ASeaGameMode::SpawnConvoy()
{
	if (ConvoySize <= 0 || !MerchantShipClass || !GetWorld())
	{
		return;
	}
	const FRotator Heading(0.f, ConvoyCourseYaw, 0.f);
	int32 Born = 0;
	for (int32 i = 0; i < ConvoyStations.Num(); ++i)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APawn* Pawn = GetWorld()->SpawnActor<APawn>(MerchantShipClass,
			ConvoyStations[i], Heading, Params);
		AShipPawn* Ship = Cast<AShipPawn>(Pawn);
		UE_LOG(LogTemp, Display, TEXT("CONVOYLOG merchant spawned=%s station=%d at %s"),
			Ship ? *Ship->GetName() : TEXT("FAILED"), i, *ConvoyStations[i].ToCompactString());
		if (!Ship)
		{
			continue;
		}
		if (!Ship->GetController())
		{
			Ship->SpawnDefaultController();
		}
		if (AMerchantShipPawn* Laden = Cast<AMerchantShipPawn>(Ship))
		{
			Laden->SetCargoValue(ConvoyCargo);
		}
		if (AShipAIController* AI = Cast<AShipAIController>(Ship->GetController()))
		{
			AI->SetDestination(Landfall);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CONVOYLOG %s has no captain and will drift"),
				*Ship->GetName());
		}
		Convoy.Add(Ship);
		BindShip(Ship);
		Ship->OnShipStruck.AddUObject(this, &ASeaGameMode::HandleShipStruck);
		++Born;
	}
	UE_LOG(LogTemp, Display, TEXT("CONVOYLOG convoy of %d stood out for landfall (%.0f,%.0f)"),
		Born, Landfall.X, Landfall.Y);
	GetWorldTimerManager().SetTimer(GaugeTimer, this,
		&ASeaGameMode::SampleWeatherGauge, 1.f, true);
	GetWorldTimerManager().SetTimer(PrizeTimer, this,
		&ASeaGameMode::SamplePrizes, 0.5f, true);
}

void ASeaGameMode::SamplePrizes()
{
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Convoy)
	{
		AShipPawn* Prize = Ptr.Get();
		// A ship that has struck and is still afloat is takeable. One that
		// sank, made port, or is already manned is not.
		if (!IsValid(Prize) || !Prize->HasStruck() || Prize->IsSunk()
			|| Prize->IsPrize())
		{
			continue;
		}

		// The nearest hunter: any hull that is not a merchant and is still in
		// the fight. The player's own hull counts, and needs no flag: she
		// takes a prize by sailing up to it, which is what a person would do.
		AShipPawn* Taker = nullptr;
		float BestM = TNumericLimits<float>::Max();
		for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
		{
			AShipPawn* Hull = *It;
			if (!IsValid(Hull) || Hull == Prize
				|| Hull->GetAllegiance() == EShipAllegiance::Merchant
				|| Hull->IsOutOfTheFight())
			{
				continue;
			}
			const float M = FVector::Dist2D(Hull->GetActorLocation(),
				Prize->GetActorLocation()) * 0.01f;
			if (M < BestM)
			{
				BestM = M;
				Taker = Hull;
			}
		}
		if (!Taker)
		{
			continue;
		}
		if (PrizeClosestM < 0.f || BestM < PrizeClosestM)
		{
			PrizeClosestM = BestM;
		}
		if (BestM > PrizeRangeM)
		{
			continue;
		}

		// Time spent within hail, ACCUMULATED. See PrizeBoatSeconds for why
		// this does not reset: a continuous dwell would be a rule satisfied
		// only by station-keeping nobody has ever asked this captain for.
		float& Spent = PrizeBoatTime.FindOrAdd(Prize);
		Spent += 0.5f;
		if (Spent < PrizeBoatSeconds)
		{
			continue;
		}

		const float Now = GetWorld()->GetTimeSeconds();
		if (Taker->DetachPrizeCrew(PrizeCrewHands))
		{
			Prize->ManAsPrize(Taker, PrizeCrewHands);
			++PrizesManned;
			HandsOutInPrizes = Taker->GetHandsInPrizes();
			UE_LOG(LogTemp, Display,
				TEXT("PRIZELOG %s manned by=%s crew=%d closest=%.0fm spent=%.1f t=%.1f manned=%d handsOut=%d"),
				*Prize->GetName(), *Taker->GetName(), PrizeCrewHands, BestM,
				Spent, Now, PrizesManned, HandsOutInPrizes);
		}
		else if (!RefusedPrizes.Contains(Prize))
		{
			// Latched per prize, or a half-second timer would print this four
			// hundred times and the counter would measure the timer.
			RefusedPrizes.Add(Prize);
			++PrizesRefused;
			UE_LOG(LogTemp, Warning,
				TEXT("PRIZELOG %s REFUSED by=%s: %d hands aboard, %d would be left, floor is %d t=%.1f refused=%d"),
				*Prize->GetName(), *Taker->GetName(), Taker->GetHands(),
				Taker->GetHands() - PrizeCrewHands, Taker->GetMinHandsAboard(),
				Now, PrizesRefused);
		}
	}
}

AShipPawn* ASeaGameMode::GetRaider() const
{
	if (!RaiderSide.IsEmpty())
	{
		for (const TWeakObjectPtr<AShipPawn>& Ptr : Squadron)
		{
			if (AShipPawn* Ship = Ptr.Get())
			{
				return Ship;
			}
		}
		return nullptr;
	}
	return PlayerShip.Get();
}

AShipPawn* ASeaGameMode::NearestMerchantInTheFight(const FVector& From) const
{
	AShipPawn* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Convoy)
	{
		AShipPawn* Ship = Ptr.Get();
		if (!IsValid(Ship) || Ship->IsOutOfTheFight())
		{
			continue;
		}
		const float Dist = FVector::DistSquared2D(From, Ship->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = Ship;
		}
	}
	return Best;
}

void ASeaGameMode::SampleWeatherGauge()
{
	if (bMissionOver)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();

	// Anyone in the roadstead is safe. Checked here, once a second, rather
	// than by the merchant herself: the game mode laid the course and owns
	// the tally, and a second's latency on "made port" is nothing.
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Convoy)
	{
		AShipPawn* Ship = Ptr.Get();
		if (!IsValid(Ship) || Ship->IsOutOfTheFight())
		{
			continue;
		}
		if (FVector::Dist2D(Ship->GetActorLocation(), Landfall) <= LandfallRadiusCm)
		{
			Ship->MakePort();
			++ConvoyThrough;
			UE_LOG(LogTemp, Display,
				TEXT("CONVOYLOG %s made port t=%.1f through=%d of %d"),
				*Ship->GetName(), Now, ConvoyThrough, ConvoySize);
		}
	}
	if (ConvoyThrough + ConvoySunk > ConvoySize - ConvoyNeed)
	{
		FinishMission(TEXT("THROUGH"));
		return;
	}

	AShipPawn* Raider = GetRaider();
	if (!Raider || Raider->IsSunk())
	{
		return;
	}
	if (!bRaiderLogged)
	{
		bRaiderLogged = true;
		UE_LOG(LogTemp, Display, TEXT("CONVOYLOG counters follow raider=%s side=%s"),
			*Raider->GetName(), RaiderSide.IsEmpty() ? TEXT("player") : *RaiderSide);
	}
	AShipPawn* Chase = NearestMerchantInTheFight(Raider->GetActorLocation());
	const UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>();
	if (!Chase || !Wind)
	{
		return;
	}

	// The weather gauge: the wind blows FROM the raider TOWARDS her chase.
	// Beating: the bearing to the chase lies inside the cone the rig cannot
	// sail, no-go plus the same margin the captain uses.
	const FVector Bearing = (Chase->GetActorLocation() - Raider->GetActorLocation()).GetSafeNormal2D();
	const FVector Downwind = Wind->GetWindDirection().GetSafeNormal2D();
	if (FVector::DotProduct(Bearing, Downwind) > 0.f)
	{
		++GaugeTicks;
	}
	else
	{
		++LeeTicks;
	}
	const float WindFrom = FMath::UnwindDegrees(Wind->GetWindBearingDeg() + 180.f);
	const float ToWind = FMath::Abs(FMath::FindDeltaAngleDegrees(Bearing.Rotation().Yaw, WindFrom));
	if (ToWind < Raider->GetNoGoAngleDeg() + BeatMarginDeg)
	{
		++BeatSeconds;
	}
}

void ASeaGameMode::HandleShipStruck(AShipPawn* Ship, AActor* Causer)
{
	// Like HandleShipSunk this can run inside a physics hit callback, so it
	// counts, logs and finishes; it spawns and destroys nothing.
	const float Now = GetWorld()->GetTimeSeconds();
	++ConvoyStopped;
	if (FirstStrikeAt < 0.f)
	{
		FirstStrikeAt = Now;
	}
	UE_LOG(LogTemp, Display,
		TEXT("CONVOYLOG %s struck by=%s t=%.1f stopped=%d of %d need=%d"),
		*Ship->GetName(), Causer ? *Causer->GetName() : TEXT("none"), Now,
		ConvoyStopped, ConvoySize, ConvoyNeed);

	// WHAT SHE IS WORTH, banked here and not at some later possession. A ship
	// that has hauled down her colours is a prize; sailing her home is the
	// next slice, and hanging the money on a possession that does not exist
	// yet would put the whole of the economy behind machinery nobody has
	// measured. Value = cargo x how much of her hull is still sound, so the
	// choice the guns already offer - aloft or into the hull - is worth
	// money for the first time: dismasting her leaves the hold dry, hulling
	// her lets the sea at it. The purse belongs to the raiding side; in every
	// scenario there is exactly one hunter, and when there is more than one
	// this becomes a question worth asking properly.
	if (const AMerchantShipPawn* Laden = Cast<AMerchantShipPawn>(Ship))
	{
		const float Sound = FMath::Clamp(
			Ship->GetHullIntegrity() / FMath::Max(1.f, Ship->GetMaxHullIntegrity()), 0.f, 1.f);
		const int32 Value = FMath::RoundToInt(Laden->GetCargoValue() * Sound);
		Purse += Value;
		++PrizesTaken;
		PrizeValueMax = FMath::Max(PrizeValueMax, Value);
		// hull= and rig= on the same line as the value they produced: a
		// derived number with its ingredients beside it can be read for a
		// transcription bug, and the zone says which of the two thresholds
		// brought her to strike.
		UE_LOG(LogTemp, Display,
			TEXT("PRIZELOG %s taken value=%d cargo=%d hull=%.2f rig=%.2f zone=%s t=%.1f purse=%d prizes=%d"),
			*Ship->GetName(), Value, Laden->GetCargoValue(), Sound,
			Ship->GetRigEfficiency(),
			Ship->GetRigEfficiency() <= Ship->GetStrikeBelowRig() ? TEXT("rig") : TEXT("hull"),
			Now, Purse, PrizesTaken);
	}

	if (ConvoyStopped >= ConvoyNeed)
	{
		FinishMission(TEXT("TAKEN"));
	}
}

void ASeaGameMode::FinishMission(const TCHAR* Result)
{
	if (bMissionOver || ConvoySize <= 0)
	{
		return;
	}
	bMissionOver = true;
	MissionResult = Result;
	GetWorldTimerManager().ClearTimer(GaugeTimer);
	const AShipPawn* Raider = GetRaider();
	UE_LOG(LogTemp, Display,
		TEXT("CONVOYLOG MISSION %s t=%.1f stopped=%d through=%d sunk=%d of %d need=%d gauge=%d lee=%d beat=%d firstStrike=%.1f raider=%s side=%s"),
		Result, GetWorld()->GetTimeSeconds(), ConvoyStopped, ConvoyThrough, ConvoySunk,
		ConvoySize, ConvoyNeed, GaugeTicks, LeeTicks, BeatSeconds, FirstStrikeAt,
		Raider ? *Raider->GetName() : TEXT("none"),
		RaiderSide.IsEmpty() ? TEXT("player") : *RaiderSide);
}

void ASeaGameMode::StrikeMerchantForTest()
{
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Convoy)
	{
		AShipPawn* Ship = Ptr.Get();
		if (!IsValid(Ship) || Ship->IsOutOfTheFight())
		{
			continue;
		}
		UE_LOG(LogTemp, Display, TEXT("CONVOYLOG strike test: %s strikes t=%.1f"),
			*Ship->GetName(), GetWorld()->GetTimeSeconds());
		Ship->Strike(this);
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("CONVOYLOG strike test: no merchant left running"));
}

void ASeaGameMode::QuitNow()
{
	// A run that ended neither way still prints its one MISSION line, with
	// the counters as they stood.
	FinishMission(TEXT("UNRESOLVED"));
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		// A counted zero. "No island, so the ground force cannot have fired"
		// is an argument; this is a measurement.
		UE_LOG(LogTemp, Display,
			TEXT("SEALOG %s groundForceTicks=%d aground=%d deepest=%.0f"),
			*It->GetName(), It->GetGroundForceTicks(), It->IsAground() ? 1 : 0,
			It->GetDeepestPenetrationCm());
	}
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		// A SEPARATE line from the grounding one, so no existing log format
		// changes and a no-island run can be diffed byte for byte against the
		// build before this slice.
		if (const AShipAIController* AI = Cast<AShipAIController>(It->GetController()))
		{
			UE_LOG(LogTemp, Display,
				TEXT("SEALOG %s landTicks=%d clawOffs=%d rejoinTicks=%d avoidTicks=%d pursuitTicks=%d prizeTicks=%d"),
				*It->GetName(), AI->GetLandTicks(), AI->GetClawOffs(),
				AI->GetRejoinTicks(), AI->GetAvoidTicks(), AI->GetPursuitTicks(),
				AI->GetPrizeTicks());
		}
	}
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		// The hands, at the end: who was lost, who was sent to repair, and
		// what they gave back. One line per hull so the gate can read the
		// enemy's and the player's apart.
		UE_LOG(LogTemp, Display,
			TEXT("CREWLOG %s hands=%d/%d casualties=%d repairShare=%.2f repaired=%.3f gunCrew=%.2f rig=%.2f rudder=%.2f"),
			*It->GetName(), It->GetHands(), It->GetHandsMax(), It->GetCasualties(),
			It->GetRepairShare(), It->GetRepairedTotal(), It->GetGunCrewFactor(),
			It->GetRigEfficiency(), It->GetRudderIntegrity());
	}
	// ALWAYS, even in a run with no convoy in it: a counted zero. A money
	// counter that is simply absent from thirteen scenarios cannot be told
	// from one that stopped being written.
	UE_LOG(LogTemp, Display,
		TEXT("PRIZELOG PURSE purse=%d prizes=%d valueMax=%d cargo=%d manned=%d refused=%d handsOut=%d closest=%.0f"),
		Purse, PrizesTaken, PrizeValueMax, ConvoyCargo, PrizesManned,
		PrizesRefused, HandsOutInPrizes, PrizeClosestM);
	UE_LOG(LogTemp, Display, TEXT("SEALOG quitting at t=%.1fs"),
		GetWorld()->GetTimeSeconds());
	if (GEngine)
	{
		GEngine->Exec(GetWorld(), TEXT("quit"));
	}
}

void ASeaGameMode::ScuttlePlayerForTest()
{
	if (AShipPawn* Ship = PlayerShip.Get())
	{
		UE_LOG(LogTemp, Display, TEXT("SEALOG sink test: scuttling %s side=%s t=%.1f"),
			*Ship->GetName(), bSinkTestStarboard ? TEXT("starboard") : TEXT("port"),
			GetWorld()->GetTimeSeconds());
		Ship->ScuttleHull(bSinkTestStarboard, this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SEALOG sink test: no player ship"));
	}
}

void ASeaGameMode::ScuttleEnemyForTest()
{
	// Every one of them, so the test still means "the enemy goes down" when
	// the enemy is a squadron rather than a ship.
	int32 Scuttled = 0;
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Squadron)
	{
		AShipPawn* Ship = Ptr.Get();
		if (!IsValid(Ship) || Ship->IsSinking())
		{
			continue;
		}
		UE_LOG(LogTemp, Display, TEXT("SEALOG sink test: scuttling %s side=%s t=%.1f"),
			*Ship->GetName(), bSinkTestStarboard ? TEXT("starboard") : TEXT("port"),
			GetWorld()->GetTimeSeconds());
		Ship->ScuttleHull(bSinkTestStarboard, this);
		++Scuttled;
	}
	if (Scuttled == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SEALOG sink test: no enemy ship"));
	}
}
