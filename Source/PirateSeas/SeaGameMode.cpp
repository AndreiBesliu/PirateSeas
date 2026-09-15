#include "SeaGameMode.h"

#include "EnemyShipPawn.h"
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

void ASeaGameMode::QuitNow()
{
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
				TEXT("SEALOG %s landTicks=%d clawOffs=%d rejoinTicks=%d avoidTicks=%d"),
				*It->GetName(), AI->GetLandTicks(), AI->GetClawOffs(),
				AI->GetRejoinTicks(), AI->GetAvoidTicks());
		}
	}
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
