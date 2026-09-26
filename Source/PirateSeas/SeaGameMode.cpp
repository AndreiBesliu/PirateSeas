#include "SeaGameMode.h"

#include "EnemyShipPawn.h"
#include "GunSmoke.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "HullSplinters.h"
#include "MuzzleFlash.h"
#include "MerchantShipPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Island.h"
#include "WindSubsystem.h"
#include "OceanSurface.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ShipHUD.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "ShipAIController.h"
#include "ShipPawn.h"
#include "AimIndicator.h"
#include "ShotTrail.h"
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
	// The gunnery sounds and their attenuations, as hard references: see the
	// header. Order matches ESeaSound.
	{
		static ConstructorHelpers::FObjectFinder<USoundBase> W0(TEXT("/Game/Sounds/S_Cannon.S_Cannon"));
		static ConstructorHelpers::FObjectFinder<USoundBase> W1(TEXT("/Game/Sounds/S_Hit.S_Hit"));
		static ConstructorHelpers::FObjectFinder<USoundBase> W2(TEXT("/Game/Sounds/S_Rig.S_Rig"));
		static ConstructorHelpers::FObjectFinder<USoundBase> W3(TEXT("/Game/Sounds/S_Splash.S_Splash"));
		static ConstructorHelpers::FObjectFinder<USoundAttenuation> A0(TEXT("/Game/Sounds/ATT_S_Cannon.ATT_S_Cannon"));
		static ConstructorHelpers::FObjectFinder<USoundAttenuation> A1(TEXT("/Game/Sounds/ATT_S_Hit.ATT_S_Hit"));
		static ConstructorHelpers::FObjectFinder<USoundAttenuation> A2(TEXT("/Game/Sounds/ATT_S_Rig.ATT_S_Rig"));
		static ConstructorHelpers::FObjectFinder<USoundAttenuation> A3(TEXT("/Game/Sounds/ATT_S_Splash.ATT_S_Splash"));
		SeaWaves = { W0.Object, W1.Object, W2.Object, W3.Object };
		SeaAttenuations = { A0.Object, A1.Object, A2.Object, A3.Object };
	}
	DefaultPawnClass = AShipPawn::StaticClass();
	EnemyShipClass = AEnemyShipPawn::StaticClass();
	MerchantShipClass = AMerchantShipPawn::StaticClass();
	HUDClass = AShipHUD::StaticClass();
}

namespace
{
	/** One page of the ship's book, as written or as read. */
	struct FBookPage
	{
		bool bOk = false;
		bool bShip = false;
		int32 Hands = 0;
		float Hull = 0.f;
		int32 Shot = 0;
		int32 Tackle = 0;
		int32 Order = 0;
		int32 Cruises = 0;
		int32 Wrecks = 0;
		int32 Chest = 0;

		bool operator==(const FBookPage& O) const
		{
			return bOk == O.bOk && bShip == O.bShip && Hands == O.Hands && Hull == O.Hull
				&& Shot == O.Shot && Tackle == O.Tackle && Order == O.Order
				&& Cruises == O.Cruises && Wrecks == O.Wrecks && Chest == O.Chest;
		}
	};

	/** A book's number: digits only, one to nine of them. Anything else - a
	 *  sign, a decimal point, a word, a number that would overflow - makes the
	 *  page not a book. The first reader took garbage as 0 and a ten-digit
	 *  chest as whatever Atoi made of it. */
	bool ReadBookInt(const FString* V, int32& Out)
	{
		if (!V || V->Len() < 1 || V->Len() > 9)
		{
			return false;
		}
		for (const TCHAR Ch : *V)
		{
			if (!FChar::IsDigit(Ch))
			{
				return false;
			}
		}
		Out = FCString::Atoi(**V);
		return true;
	}

	/** key=value, one per line, and nothing cleverer - not FParse::Value over
	 *  the text, which finds a name wherever the character before it is not a
	 *  letter or digit. A book is book=1 first and end=1 last (a page torn by a
	 *  quit that died mid-write has no end), with every number well formed; the
	 *  ship is all three of hands, hull and shot, or none of them. */
	FBookPage ReadBookPage(const FString& Text)
	{
		TMap<FString, FString> Kv;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		for (const FString& Raw : Lines)
		{
			FString Key, Value;
			if (Raw.TrimStartAndEnd().Split(TEXT("="), &Key, &Value))
			{
				Kv.Add(Key.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
			}
		}
		FBookPage P;
		const FString* Book = Kv.Find(TEXT("book"));
		const FString* End = Kv.Find(TEXT("end"));
		if (!Book || *Book != TEXT("1") || !End || *End != TEXT("1")
			|| !ReadBookInt(Kv.Find(TEXT("cruises")), P.Cruises)
			|| !ReadBookInt(Kv.Find(TEXT("wrecks")), P.Wrecks)
			|| !ReadBookInt(Kv.Find(TEXT("chest")), P.Chest))
		{
			return FBookPage();
		}
		const FString* H = Kv.Find(TEXT("hands"));
		const FString* V = Kv.Find(TEXT("hull"));
		const FString* S = Kv.Find(TEXT("shot"));
		// The tackle and an open order belong to the ship: OPTIONAL (a book
		// written before the port sold tackle has neither, and is still a
		// book), but never without the ship they are on.
		const FString* T = Kv.Find(TEXT("tackle"));
		const FString* Ord = Kv.Find(TEXT("order"));
		if (H || V || S || T || Ord)
		{
			int32 Hull = 0;
			if (!ReadBookInt(H, P.Hands) || !ReadBookInt(V, Hull) || !ReadBookInt(S, P.Shot)
				|| (T && !ReadBookInt(T, P.Tackle)) || (Ord && !ReadBookInt(Ord, P.Order)))
			{
				return FBookPage();
			}
			P.Hull = (float)Hull;
			P.bShip = true;
		}
		P.bOk = true;
		return P;
	}

	FString WriteBookPage(const FBookPage& P)
	{
		FString T = FString::Printf(TEXT("book=1\ncruises=%d\nwrecks=%d\nchest=%d\n"),
			P.Cruises, P.Wrecks, P.Chest);
		if (P.bShip)
		{
			T += FString::Printf(TEXT("hands=%d\nhull=%.0f\nshot=%d\ntackle=%d\norder=%d\n"),
				P.Hands, P.Hull, P.Shot, P.Tackle, P.Order);
		}
		return T + TEXT("end=1\n");
	}

	const TCHAR* BookSlotName(bool bOff, bool bGiven)
	{
		return bOff ? TEXT("off") : (bGiven ? TEXT("given") : TEXT("default"));
	}
}

int32 ASeaGameMode::SoundRequests[4] = { 0, 0, 0, 0 };
int32 ASeaGameMode::SoundMissing = 0;
bool ASeaGameMode::bSoundEnabled = true;
bool ASeaGameMode::bSoundFlagRead = false;

void ASeaGameMode::ResetSoundsForNewLevel()
{
	for (int32& N : SoundRequests)
	{
		N = 0;
	}
	SoundMissing = 0;
	bSoundFlagRead = false;
}

void ASeaGameMode::PlaySea(UWorld* World, ESeaSound Which, const FVector& Where)
{
	if (!bSoundFlagRead)
	{
		int32 Flag = 1;
		if (FParse::Value(FCommandLine::Get(), TEXT("ShipSound="), Flag))
		{
			bSoundEnabled = Flag != 0;
		}
		bSoundFlagRead = true;
	}
	if (!bSoundEnabled || !World)
	{
		return;
	}
	++SoundRequests[(int32)Which];

	// Off the game mode's own properties, found in its constructor, so the cook
	// carries them. The first version loaded them by path at play time, which
	// the cook cannot see: the packaged game would have been silent.
	const int32 I = (int32)Which;
	const ASeaGameMode* Sea = World->GetAuthGameMode<ASeaGameMode>();
	USoundBase* Wave = (Sea && Sea->SeaWaves.IsValidIndex(I)) ? Sea->SeaWaves[I].Get() : nullptr;
	USoundAttenuation* Att = (Sea && Sea->SeaAttenuations.IsValidIndex(I))
		? Sea->SeaAttenuations[I].Get() : nullptr;
	static bool bMissingReported[4] = { false, false, false, false };
	if (!Wave)
	{
		// COUNTED EVERY TIME (the suite reads sound_missing and requires zero)
		// and said once for a human. A review found the first version counted
		// the request and not the miss, so an absent asset gave the same four
		// numbers as a present one and the suite could not tell.
		++SoundMissing;
		if (!bMissingReported[I])
		{
			bMissingReported[I] = true;
			UE_LOG(LogTemp, Error, TEXT("SOUNDLOG missing sound %d"), I);
		}
		return;
	}
	UGameplayStatics::PlaySoundAtLocation(World, Wave, Where, 1.f, 1.f, 0.f, Att);
}

void ASeaGameMode::BeginPlay()
{
	Super::BeginPlay();
	ResetSoundsForNewLevel();

	// The smoke's statics, before anything can spawn a puff. A list and three
	// counters that outlive a level do not fail, they answer - with last
	// level's numbers.
	AGunSmoke::ResetForNewLevel();
	AMuzzleFlash::ResetForNewLevel();
	AHullSplinters::ResetForNewLevel();
	AShipPawn::ResetHoleTotalsForNewLevel();

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
	// AFTER the convoy, because the roadstead is laid relative to where the
	// convoy is sighted - and OUTSIDE it, because a port does not need one.
	ReadPortFlags();
	if (bHasPort)
	{
		StartPrizeTimer();
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
		FParse::Value(FCommandLine::Get(), TEXT("ConvoySinkTest="), ConvoySinkTestAt);
		if (ConvoySinkTestAt > 0.f)
		{
			GetWorldTimerManager().SetTimer(ConvoySinkTestTimer, this,
				&ASeaGameMode::SinkMerchantForTest, ConvoySinkTestAt, false);
		}
		if (ConvoyStrikeTestAt > 0.f)
		{
			GetWorldTimerManager().SetTimer(ConvoyStrikeTestTimer, this,
				&ASeaGameMode::StrikeMerchantForTest, ConvoyStrikeTestAt, false);
		}
	}

	FParse::Value(FCommandLine::Get(), TEXT("ShipSinkTest="), ShipSinkTestAt);
	FParse::Value(FCommandLine::Get(), TEXT("EnemySinkTest="), EnemySinkTestAt);
	FParse::Value(FCommandLine::Get(), TEXT("EnemyStrikeTest="), EnemyStrikeTestAt);
	if (EnemyStrikeTestAt > 0.f)
	{
		GetWorldTimerManager().SetTimer(EnemyStrikeTestTimer, this,
			&ASeaGameMode::StrikeEnemyForTest, EnemyStrikeTestAt, false);
	}
	FParse::Value(FCommandLine::Get(), TEXT("EnemyBreakTest="), EnemyBreakTestAt);
	if (EnemyBreakTestAt > 0.f)
	{
		GetWorldTimerManager().SetTimer(EnemyBreakTestTimer, this,
			&ASeaGameMode::BreakEnemyForTest, EnemyBreakTestAt, false);
	}
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
	if (bHasPort)
	{
		// An island on the roadstead is not a scenario either: it is a prize
		// sailed onto a beach by her own orders.
		Births.Add(PortLocation);
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

void ASeaGameMode::EnsureBookRead()
{
	if (bBookRead)
	{
		return;
	}
	bBookRead = true;

	// WHICH FILE. -LedgerBook= first, because the suite pins -Ledger=0 on every
	// row and its own book rows still have to open one; then -Ledger=0; then the
	// slot every player gets. The name is said out loud on every row, and the
	// gate refuses "default" anywhere in the suite: a renamed flag would
	// otherwise open the owner's own book under a measurement.
	FString Given;
	int32 On = 1;
	if (FParse::Value(FCommandLine::Get(), TEXT("LedgerBook="), Given) && !Given.IsEmpty())
	{
		BookSlot = EBookSlot::Given;
		BookPath = FPaths::IsRelative(Given) ? FPaths::Combine(FPaths::ProjectDir(), Given) : Given;
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("Ledger="), On) && On == 0)
	{
		BookSlot = EBookSlot::Off;
	}
	else
	{
		BookSlot = EBookSlot::Default;
		BookPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Ledger"), TEXT("book.txt"));
	}
	if (BookSlot == EBookSlot::Off)
	{
		return;
	}
	// ONLY WHERE A LOSS CAN BE MADE GOOD. The review walked the owner through
	// his own list: every check on it runs without a port, the book opened
	// anyway, and a magazine carried out of one test came back empty in the
	// next with nothing on the sea able to refill it.
	int32 TestShot = 0;
	float TestHull = 0.f;
	int32 TestToggle = 0;
	if (!PortRequested())
	{
		BookSlot = EBookSlot::Off;
		BookWhy = TEXT("noport");
		UE_LOG(LogTemp, Display, TEXT("LEDGERLOG shut: no roadstead in this run, nothing to carry a ship to"));
		return;
	}
	float TestBreak = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("Shot="), TestShot)
		|| FParse::Value(FCommandLine::Get(), TEXT("ShipHullTest="), TestHull)
		|| FParse::Value(FCommandLine::Get(), TEXT("ShipToggleTackle="), TestToggle)
		|| FParse::Value(FCommandLine::Get(), TEXT("EnemyBreakTest="), TestBreak))
	{
		BookSlot = EBookSlot::Off;
		BookWhy = TEXT("testflag");
		UE_LOG(LogTemp, Display, TEXT("LEDGERLOG shut: -Shot=, -ShipHullTest= or -ShipToggleTackle= sets the ship by hand, and a test is not a cruise"));
		return;
	}
	BookWhy = TEXT("open");
	FPaths::NormalizeFilename(BookPath);

	// A QUIT THAT DIED MID-REPLACE. The writer puts the new page beside the
	// book and then moves it over; on Windows the move deletes the old one
	// first. Book gone and the new page whole beside it: that page is the book.
	const FString Tmp = BookPath + TEXT(".tmp");
	FString ReadFrom = BookPath;
	if (!FPaths::FileExists(BookPath))
	{
		if (!FPaths::FileExists(Tmp))
		{
			UE_LOG(LogTemp, Display, TEXT("LEDGERLOG no book at %s: a first cruise"),
				*FPaths::ConvertRelativePathToFull(BookPath));
			return;
		}
		ReadFrom = Tmp;
		bBookRecovered = true;
		UE_LOG(LogTemp, Warning, TEXT("LEDGERLOG no book, but a whole page beside it: the last quit died replacing it, recovered"));
	}
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ReadFrom))
	{
		// UNREAD IS NOT REFUSED. Locked, or a disk that failed: the file stays
		// exactly where it is, and nothing is written over it at the quit.
		++BookRejected;
		bBookWritable = false;
		UE_LOG(LogTemp, Warning, TEXT("LEDGERLOG %s could not be read: left alone, and not written this run"),
			*FPaths::ConvertRelativePathToFull(ReadFrom));
		return;
	}
	const FBookPage Page = ReadBookPage(Text);
	if (!Page.bOk)
	{
		// REFUSED WHOLE, never half-read, and KEPT: moved aside to .rejected
		// so the quit can write a good page without destroying the evidence of
		// the bad one. If it cannot be moved, it is not written over either.
		++BookRejected;
		bBookSetAside = IFileManager::Get().Move(*(BookPath + TEXT(".rejected")), *ReadFrom, true, true);
		bBookWritable = bBookSetAside;
		UE_LOG(LogTemp, Warning, TEXT("LEDGERLOG %s is not a book: refused, %s, she sails as built"),
			*FPaths::ConvertRelativePathToFull(ReadFrom),
			bBookSetAside ? TEXT("set aside as .rejected") : TEXT("could not be set aside, and will not be written over"));
		return;
	}
	bBookLoaded = true;
	bBookShip = Page.bShip;
	BookTackle = Page.Tackle;
	BookOrder = Page.Order;
	BookHands = Page.Hands;
	BookHull = Page.Hull;
	BookShot = Page.Shot;
	BookCruises = FMath::Max(0, Page.Cruises);
	BookWrecks = FMath::Max(0, Page.Wrecks);
	ChestIn = FMath::Max(0, Page.Chest);
}

void ASeaGameMode::FitFromBook(AShipPawn* Ship)
{
	// The player's hull and nobody else's. Allegiance, not IsPlayerControlled:
	// a replacement runs BeginPlay inside SpawnActor, before it is possessed,
	// and would read as a stranger - which is exactly the hull the latch below
	// has to see.
	if (!Ship || Ship->GetAllegiance() != EShipAllegiance::Player)
	{
		return;
	}
	EnsureBookRead();
	if (BookSlot == EBookSlot::Off)
	{
		return;
	}
	if (bBookFitted)
	{
		// A NEW SHIP IS A NEW SHIP. The book fitted the hull that started the
		// cruise; a replacement after a sinking was bought whole (see
		// HandleShipSunk) and must not inherit the old one's wounds or stores.
		++BookRefused;
		UE_LOG(LogTemp, Display, TEXT("LEDGERLOG %s refused: the book fitted %s, one hull per run"),
			*Ship->GetName(), *BookFittedTo);
		return;
	}
	bBookFitted = true;
	BookFittedTo = Ship->GetName();
	if (bBookLoaded && bBookShip)
	{
		BookClamped = Ship->FitFromBook(BookHands, BookHull, BookShot, BookTackle, BookOrder);
	}
	// READ BACK OFF THE HULL, not off the page: the page is what was asked, the
	// hull is what she got. The -EnemyHull lesson - a value written and then
	// quietly overwritten reads fine in the line that wrote it.
	UE_LOG(LogTemp, Display,
		TEXT("LEDGERLOG OPEN ship=%s loaded=%d rejected=%d cruise=%d hands=%d/%d hull=%.0f/%.0f shot=%d/%d chest=%d wrecks=%d clamped=%d tackle=%d/%d order=%d"),
		*Ship->GetName(), bBookLoaded ? 1 : 0, BookRejected, GetCruise(),
		Ship->GetHands(), Ship->GetHandsMax(), Ship->GetHullIntegrity(), Ship->GetMaxHullIntegrity(),
		Ship->GetShot(), Ship->GetShotMax(), ChestIn, BookWrecks, BookClamped,
		Ship->GetTackleTier(), Ship->GetTackleMaxTier(), Ship->IsTackleOrdered() ? 1 : 0);
}

bool ASeaGameMode::FillTackleOrder(AShipPawn* Ship, int32 Coffers)
{
	// Not yet: a key-placed order still in its grace. Not a refusal - the
	// order stays open, and the repairs go on in this tick.
	const float Now = GetWorld()->GetTimeSeconds();
	if (!Ship->IsTackleOrderPayable(Now))
	{
		return false;
	}
	const int32 Price = (Ship->GetTackleTier() + 1) * TackleCost;
	if (Coffers < Price)
	{
		++TackleRefusedCoffers;
		Ship->RefuseTackleOrder(Price);
		UE_LOG(LogTemp, Display, TEXT("TACKLELOG %s order refused t=%.1f: tier %d costs %d, the coffers hold %d"),
			*Ship->GetName(), Now, Ship->GetTackleTier() + 1, Price, Coffers);
		return false;
	}
	Spent += Price;
	TackleSpent += Price;
	++TackleBought;
	Ship->FitTackleTier();
	return true;
}

bool ASeaGameMode::PortRequested()
{
	int32 PortOn = 0;
	return FParse::Value(FCommandLine::Get(), TEXT("Port="), PortOn) && PortOn > 0;
}

bool ASeaGameMode::IsPurseSide(const AShipPawn* Ship) const
{
	if (!Ship)
	{
		return false;
	}
	return RaiderSide.IsEmpty()
		? Ship->GetAllegiance() == EShipAllegiance::Player
		: Ship->GetAllegiance() == EShipAllegiance::Crown;
}

bool ASeaGameMode::IsChestInPurse() const
{
	return BookSlot != EBookSlot::Off && bHasPort && RaiderSide.IsEmpty();
}

int32 ASeaGameMode::GetCoffers() const
{
	// Prize money landed, less what it bought; and, where the player owns the
	// purse and there is a roadstead, the chest the book brought ashore less
	// what lost ships have cost. Never below zero: every purchase is checked
	// against this, and a negative would buy negative hull.
	int32 C = Landed - Spent;
	if (IsChestInPurse())
	{
		C += ChestIn - WreckCharge;
	}
	return FMath::Max(0, C);
}

int32 ASeaGameMode::GetChestOut() const
{
	return IsChestInPurse() ? GetCoffers() : FMath::Max(0, ChestIn - WreckCharge);
}

int32 ASeaGameMode::NewShipCost(const AShipPawn* Ship) const
{
	return FMath::RoundToInt(Ship->GetMaxHullIntegrity() * HullPointCost)
		+ Ship->GetHandsMax() * HandCost + Ship->GetShotMax() * ShotCost;
}

void ASeaGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseBook(EndPlayReason);
	Super::EndPlay(EndPlayReason);
}

void ASeaGameMode::CloseBook(EEndPlayReason::Type Reason)
{
	EnsureBookRead();
	if (bBookClosed)
	{
		// Said on a line of the same shape, so a second close is counted as
		// one: the gate asks for exactly one CLOSE per run.
		UE_LOG(LogTemp, Warning, TEXT("LEDGERLOG CLOSE again - refused, the book is written once per run"));
		return;
	}
	bBookClosed = true;

	const TCHAR* Why = Reason == EEndPlayReason::Quit ? TEXT("quit")
		: Reason == EEndPlayReason::EndPlayInEditor ? TEXT("editor")
		: Reason == EEndPlayReason::LevelTransition ? TEXT("travel") : TEXT("other");
	FBookPage Out;
	int32 Written = 0;
	int32 Roundtrip = 0;
	if (BookSlot != EBookSlot::Off && bBookWritable
		&& (Reason == EEndPlayReason::Quit || Reason == EEndPlayReason::EndPlayInEditor))
	{
		Out.bOk = true;
		Out.Cruises = GetCruise();
		Out.Wrecks = BookWrecks + WrecksThisRun;
		Out.Chest = GetChestOut();
		// THE SHIP AFLOAT NOW, or none. A hull on her way down at the quit is
		// not a ship to carry: the next cruise starts in her replacement, which
		// the chest already paid for when she went.
		const AShipPawn* Ship = PlayerShip.Get();
		if (IsValid(Ship) && !Ship->IsSunk())
		{
			Out.bShip = true;
			Out.Hands = Ship->GetHands();
			// At least 1: she is afloat, and a hull of 0 in the book reads as a
			// hand edit (the reader cuts it to 1 and counts the cut).
			Out.Hull = FMath::Max(1.f, FMath::RoundToFloat(Ship->GetHullIntegrity()));
			Out.Shot = Ship->GetShot();
			Out.Tackle = Ship->GetTackleTier();
			Out.Order = Ship->IsTackleOrdered() ? 1 : 0;
		}
		// Beside it and then over it, so a quit that dies half-way through the
		// write leaves the last good book standing rather than half of a new one.
		const FString Tmp = BookPath + TEXT(".tmp");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(BookPath), true);
		if (FFileHelper::SaveStringToFile(WriteBookPage(Out), *Tmp)
			&& IFileManager::Get().Move(*BookPath, *Tmp, true, true))
		{
			Written = 1;
			// READ BACK THROUGH THE SAME READER the next cruise will use. The
			// writer and the reader prove each other on every row that writes.
			FString Back;
			if (FFileHelper::LoadFileToString(Back, *BookPath) && ReadBookPage(Back) == Out)
			{
				Roundtrip = 1;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("LEDGERLOG could not write %s"),
				*FPaths::ConvertRelativePathToFull(BookPath));
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("LEDGERLOG CLOSE slot=%s why=%s reason=%s written=%d roundtrip=%d cruise=%d ship=%d hands=%d hull=%.0f shot=%d chest=%d wrecks=%d wreckCharge=%d refused=%d rejected=%d setAside=%d recovered=%d tackle=%d order=%d"),
		BookSlotName(BookSlot == EBookSlot::Off, BookSlot == EBookSlot::Given), *BookWhy, Why, Written, Roundtrip,
		Out.Cruises, Out.bShip ? 1 : 0, Out.Hands, Out.Hull, Out.Shot, Out.Chest, Out.Wrecks,
		WreckCharge, BookRefused, BookRejected, bBookSetAside ? 1 : 0, bBookRecovered ? 1 : 0,
		Out.Tackle, Out.Order);
}

void ASeaGameMode::BindShip(AShipPawn* Ship)
{
	Ship->OnShipSunk.AddUObject(this, &ASeaGameMode::HandleShipSunk);
	Ship->OnShipWrecked.AddUObject(this, &ASeaGameMode::HandleShipWrecked);
}

void ASeaGameMode::StrikeEnemyForTest()
{
	// The FIRST enemy still in the fight, so the ship that strikes is the one
	// the rest of the line is dressing on - which is the case that matters. A
	// consort at the tail striking proves nothing about closing up.
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Squadron)
	{
		AShipPawn* Ship = Ptr.Get();
		if (IsValid(Ship) && !Ship->IsOutOfTheFight())
		{
			// Causer is nobody: she strikes because the test says so, not because
			// anyone shot her, and a false attacker would show up in the prize
			// bookkeeping as a capture that never happened.
			Ship->Strike(nullptr);
			UE_LOG(LogTemp, Display,
				TEXT("AILOG %s STRUCK for the test at t=%.1f"),
				*Ship->GetName(), GetWorld()->GetTimeSeconds());
			return;
		}
	}
	UE_LOG(LogTemp, Warning,
		TEXT("AILOG the strike test found no enemy still in the fight"));
}

void ASeaGameMode::BreakEnemyForTest()
{
	// The FIRST ship still fighting, as the strike test: the one the rest of the
	// line dresses on.
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Squadron)
	{
		AShipPawn* Ship = Ptr.Get();
		if (IsValid(Ship) && !Ship->IsOutOfTheFight() && !AShipAIController::IsShipBreakingOff(Ship))
		{
			Ship->SetHullForTest(BreakTestHullFraction * Ship->GetMaxHullIntegrity());
			UE_LOG(LogTemp, Display, TEXT("AILOG %s BROKEN for the test at t=%.2f hull=%.0f/%.0f"),
				*Ship->GetName(), GetWorld()->GetTimeSeconds(), Ship->GetHullIntegrity(),
				Ship->GetMaxHullIntegrity());
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("AILOG the break test found no enemy still in the fight"));
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
		// A captain cannot make for a port she has never been told about.
		if (bHasPort)
		{
			if (AShipAIController* AI = Cast<AShipAIController>(Ship->GetController()))
			{
				AI->SetPort(PortLocation, PortRadiusCm);
			}
		}
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
		// A LOST SHIP IS PAID FOR, when there is a book to pay it from. The
		// replacement is a new hull bought whole at the port's own prices, and
		// the chest pays as far as it reaches - no debt. Without this, being
		// sunk would be the cheapest refit in the game: a hurt ship carried in
		// the book would come back whole for nothing.
		EnsureBookRead();
		if (BookSlot != EBookSlot::Off)
		{
			const int32 Cost = NewShipCost(Ship);
			const int32 Paid = FMath::Min(Cost, GetChestOut());
			WreckCharge += Paid;
			++WrecksThisRun;
			UE_LOG(LogTemp, Display,
				TEXT("LEDGERLOG %s lost: a new hull costs %d, the chest paid %d"),
				*Ship->GetName(), Cost, Paid);
		}
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

void ASeaGameMode::ReadPortFlags()
{
	if (!PortRequested())
	{
		return;
	}
	bHasPort = true;
	FParse::Value(FCommandLine::Get(), TEXT("PortOffingM="), PortOffingM);
	FParse::Value(FCommandLine::Get(), TEXT("HandCost="), HandCost);
	FParse::Value(FCommandLine::Get(), TEXT("HullPointCost="), HullPointCost);
	FParse::Value(FCommandLine::Get(), TEXT("ShotCost="), ShotCost);
	float PortRadiusM = PortRadiusCm * 0.01f;
	if (FParse::Value(FCommandLine::Get(), TEXT("PortRadiusM="), PortRadiusM))
	{
		PortRadiusCm = PortRadiusM * 100.f;
	}

	// Downwind of where the convoy is sighted, because twelve men do not beat
	// a laden hull home. With no convoy on the water ConvoyStart is still the
	// default sighting point, so the roadstead lands somewhere sensible rather
	// than on the origin where the player's own hull sits.
	const UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>();
	const float WindTo = Wind ? Wind->GetWindBearingDeg() : 0.f;
	const FVector Downwind = FRotator(0.f, WindTo, 0.f).Vector();
	FVector Where = ConvoyStart + Downwind * PortOffingM * 100.f;
	FParse::Value(FCommandLine::Get(), TEXT("PortX="), Where.X);
	FParse::Value(FCommandLine::Get(), TEXT("PortY="), Where.Y);
	PortLocation = FVector(
		FMath::Clamp(Where.X, -OceanHalfExtentCm, OceanHalfExtentCm),
		FMath::Clamp(Where.Y, -OceanHalfExtentCm, OceanHalfExtentCm), 0.f);
	UE_LOG(LogTemp, Display,
		TEXT("PORTLOG roadstead at (%.0f,%.0f), radius %.0f m, %.0f m downwind of the convoy"),
		PortLocation.X, PortLocation.Y, PortRadiusCm * 0.01f, PortOffingM);
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
	StartPrizeTimer();
}

void ASeaGameMode::StartPrizeTimer()
{
	// Idempotent, and called from two places: the convoy standing out, and
	// BeginPlay when there is a port but no convoy. SamplePrizes does the
	// refit as well as the prizes, so without this second call a raider with
	// a roadstead and no convoy could never spend a penny.
	if (!GetWorldTimerManager().IsTimerActive(PrizeTimer))
	{
		GetWorldTimerManager().SetTimer(PrizeTimer, this,
			&ASeaGameMode::SamplePrizes, 0.5f, true);
	}
}

void ASeaGameMode::RefitInPort()
{
	if (!bHasPort)
	{
		return;
	}
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		AShipPawn* Ship = *It;
		if (!IsValid(Ship) || Ship->GetAllegiance() == EShipAllegiance::Merchant
			|| Ship->IsSunk())
		{
			continue;
		}
		if (FVector::Dist2D(Ship->GetActorLocation(), PortLocation) > PortRadiusCm)
		{
			continue;
		}
		// THE PURSE'S SIDE ONLY. It used to sell to any hull in the circle, so a
		// Crown ship in the player's roadstead was refitted out of his money -
		// and, since the book, out of his chest.
		if (!IsPurseSide(Ship))
		{
			if (!RefusedSide.Contains(Ship->GetFName()))
			{
				RefusedSide.Add(Ship->GetFName());
				UE_LOG(LogTemp, Display, TEXT("PORTLOG %s refused: not the purse's side"), *Ship->GetName());
			}
			continue;
		}

		const int32 Coffers = GetCoffers();
		bool bBought = false;

		// Powder and shot FIRST: it is the cheapest thing on the list and the
		// one without which none of the rest matters. A whole crew on a sound
		// hull with an empty magazine is a transport.
		if (Ship->HasMagazine() && Ship->GetShot() < Ship->GetShotMax()
			&& Coffers >= ShotCost)
		{
			const int32 Afford = FMath::Min(ShotPerTick, Coffers / FMath::Max(1, ShotCost));
			const int32 Room = Ship->GetShotMax() - Ship->GetShot();
			const int32 Take = FMath::Min(Afford, Room);
			if (Take > 0 && Ship->LoadShot(Take))
			{
				Spent += Take * ShotCost;
				ShotBought += Take;
				bBought = true;
			}
		}
		// THE SHIPWRIGHT'S ORDER, after powder and shot and before men and hull:
		// a captain who ordered tackle meant the money for it, and a whole
		// magazine costs less than either. Full price or refused; a refusal
		// closes the order and falls through to the repairs in the SAME tick.
		else if (Ship->IsTackleOrdered() && FillTackleOrder(Ship, Coffers))
		{
			bBought = true;
		}
		// Then men. A ship with no crew cannot use a sound hull, and the
		// cheaper thing should be the one she gets when the money is short.
		else if (Ship->GetHandsShort() > 0 && Coffers >= HandCost && Ship->RecruitHand())
		{
			Spent += HandCost;
			++HandsBought;
			bBought = true;
		}
		else
		{
			const float Wanted = FMath::Min(
				HullPointsPerTick,
				(GetCoffers()) / FMath::Max(0.01f, HullPointCost));
			const float Put = Ship->RepairHull(Wanted);
			if (Put > 0.f)
			{
				const int32 Cost = FMath::RoundToInt(Put * HullPointCost);
				Spent += Cost;
				HullBought += Put;
				bBought = true;
			}
		}

		if (bBought)
		{
			RefitSeconds += 0.5f;
			if (!bRefitLogged)
			{
				bRefitLogged = true;
				UE_LOG(LogTemp, Display,
					TEXT("PORTLOG %s begins to refit t=%.1f coffers=%d short=%d hull=%.0f"),
					*Ship->GetName(), GetWorld()->GetTimeSeconds(), Coffers,
					Ship->GetHandsShort(), Ship->GetHullIntegrity());
			}
		}
	}
}

void ASeaGameMode::SamplePrizes()
{
	RefitInPort();

	// PRIZES COMING HOME, and note WHERE this lives. It was written first
	// inside SampleWeatherGauge, next to the merchants' own landfall, because
	// it is the same question asked of the other side. It never ran: that
	// function returns on bMissionOver and FinishMission clears its timer, and
	// the convoy is decided at about seventy seconds while a prize needs three
	// hundred to get home. Measured, and the log said it in one line - a prize
	// six metres from the quay beside landed=0. It belongs on THIS timer, the
	// one that is never cleared, and the reason that timer exists at all.
	if (bHasPort)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		for (const TWeakObjectPtr<AShipPawn>& Ptr : Convoy)
		{
			AShipPawn* Home = Ptr.Get();
			if (!IsValid(Home) || !Home->IsPrize() || Home->IsSunk())
			{
				continue;
			}
			if (FVector::Dist2D(Home->GetActorLocation(), PortLocation) > PortRadiusCm)
			{
				continue;
			}
			// The landing and the men are asked SEPARATELY: a prize reaches
			// the quay whether or not the ship that took her is still afloat
			// to receive her crew back.
			int32 Back = 0;
			if (!Home->LandPrize(Back))
			{
				continue;
			}
			++PrizesLanded;
			HandsHome += Back;
			// The money, REALISED. Purse is what she was worth when she
			// struck; this is what actually reached the quay.
			Landed += Home->GetPrizeValue();
			UE_LOG(LogTemp, Display,
				TEXT("PRIZELOG %s landed value=%d t=%.1f landed=%d of %d purse=%d handsHome=%d"),
				*Home->GetName(), Home->GetPrizeValue(), Now, PrizesLanded,
				PrizesManned, Landed, HandsHome);
		}
	}

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
		// Alongside, not Spent: the game mode now has money called Spent, and a
		// local of that name hides it. Two meanings, one word, and the compiler
		// is set to refuse that here.
		float& Alongside = PrizeBoatTime.FindOrAdd(Prize);
		Alongside += 0.5f;
		if (Alongside < PrizeBoatSeconds)
		{
			continue;
		}

		const float Now = GetWorld()->GetTimeSeconds();
		if (Taker->DetachPrizeCrew(PrizeCrewHands))
		{
			Prize->ManAsPrize(Taker, PrizeCrewHands);
			// And she sails, if there is anywhere to sail to. The same
			// controller that brought her down the coast takes her home.
			//
			// The ELSE is not tidiness. She was given a destination when she
			// was spawned - the convoy's own landfall - and striking does not
			// take it away, so without this a prize in a world with no port
			// would have stood on for the enemy's roadstead with your men
			// aboard her.
			if (AShipAIController* PrizeAI =
				Cast<AShipAIController>(Prize->GetController()))
			{
				if (bHasPort)
				{
					PrizeAI->SetDestination(PortLocation);
				}
				else
				{
					PrizeAI->ClearDestination();
				}
			}
			++PrizesManned;
			// SUMMED, not assigned. This mirrored one hull's private total into
			// a run-wide counter, so with two hunters - and SquadronSize
			// defaults to two - the second take OVERWROTE the first: handsSent
			// could fall, handsHome could exceed it, and the derived "away now"
			// went negative, which the panel then hid because it only prints
			// that clause when the number is positive. Four of the six review
			// lenses found this independently.
			HandsOutInPrizes += PrizeCrewHands;
			UE_LOG(LogTemp, Display,
				TEXT("PRIZELOG %s manned by=%s crew=%d closest=%.0fm spent=%.1f t=%.1f manned=%d handsSent=%d"),
				*Prize->GetName(), *Taker->GetName(), PrizeCrewHands, BestM,
				Alongside, Now, PrizesManned, HandsOutInPrizes);
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
		Ship->SetPrizeValue(Value);
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

void ASeaGameMode::SinkMerchantForTest()
{
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Convoy)
	{
		AShipPawn* Ship = Ptr.Get();
		if (!IsValid(Ship) || Ship->IsOutOfTheFight())
		{
			continue;
		}
		UE_LOG(LogTemp, Display, TEXT("CONVOYLOG sink test: %s founders t=%.1f"),
			*Ship->GetName(), GetWorld()->GetTimeSeconds());
		// Through the real damage path, like every other scuttle in this file,
		// so she goes down the way shot would take her down.
		Ship->ScuttleHull(true, this);
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("CONVOYLOG sink test: no merchant left afloat"));
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
				TEXT("SEALOG %s landTicks=%d clawOffs=%d rejoinTicks=%d avoidTicks=%d pursuitTicks=%d prizeTicks=%d portTicks=%d"),
				*It->GetName(), AI->GetLandTicks(), AI->GetClawOffs(),
				AI->GetRejoinTicks(), AI->GetAvoidTicks(), AI->GetPursuitTicks(),
				AI->GetPrizeTicks(), AI->GetPortTicks());
		}
	}
	// ORDERED, and not by the actor iterator. tools/ci_measure.py reads several
	// keys by name off "the EnemyShipPawn_N line", meaning the FIRST match; the
	// iterator's order is the world's, so with five enemies those keys read
	// whichever hull the level happened to hold first. Station order is both
	// stable and meaningful: the flagship prints first.
	TArray<AShipPawn*> InOrder;
	for (const TWeakObjectPtr<AShipPawn>& Ptr : Squadron)
	{
		if (AShipPawn* Ship = Ptr.Get())
		{
			InOrder.Add(Ship);
		}
	}
	for (TActorIterator<AShipPawn> Any(GetWorld()); Any; ++Any)
	{
		if (!InOrder.Contains(*Any))
		{
			InOrder.Add(*Any);
		}
	}
	for (AShipPawn* It : InOrder)
	{
		// The hands, at the end: who was lost, who was sent to repair, and
		// what they gave back. One line per hull so the gate can read the
		// enemy's and the player's apart.
		UE_LOG(LogTemp, Display,
			TEXT("SHOTLOG %s magazine shot=%d/%d fired=%d dry=%d"),
			*It->GetName(), It->GetShot(), It->GetShotMax(), It->GetShotFired(),
			It->GetDryRefusals());
		UE_LOG(LogTemp, Display,
			TEXT("TACKLELOG %s tier=%d/%d reload=%.1f ordered=%d orders=%d withdrawn=%d refusedTop=%d"),
			*It->GetName(), It->GetTackleTier(), It->GetTackleMaxTier(), It->GetReloadSeconds(),
			It->IsTackleOrdered() ? 1 : 0, It->GetTackleOrders(), It->GetTackleWithdrawn(),
			It->GetTackleRefusedTop());
		// The scars she carries at quit: added over the run, still drawn, and
		// how many the cap threw away. added summed over every hull must equal
		// hull_hits, and the gate says so.
		UE_LOG(LogTemp, Display,
			TEXT("HOLELOG %s added=%d live=%d culled=%d"),
			*It->GetName(), It->GetHolesAdded(), It->GetHolesLive(), It->GetHolesCulled());
		UE_LOG(LogTemp, Display,
			TEXT("CREWLOG %s hands=%d/%d casualties=%d repairShare=%.2f repaired=%.3f gunCrew=%.2f rig=%.2f rudder=%.2f"),
			*It->GetName(), It->GetHands(), It->GetHandsMax(), It->GetCasualties(),
			It->GetRepairShare(), It->GetRepairedTotal(), It->GetGunCrewFactor(),
			It->GetRigEfficiency(), It->GetRudderIntegrity());
	}
	// The gun smoke, at quit, whether it was on or off - a counted zero rather
	// than an absent line, same as the trail below. Until 19.09 nothing measured
	// the smoke at all: 107 keys in the baseline, none of them about it, so it
	// could have stopped spawning entirely and every scenario would have stayed
	// green. stranded is the one that must never move.
	UE_LOG(LogTemp, Display,
		TEXT("SMOKELOG TOTAL spawned=%d live=%d culled=%d stranded=%d"),
		AGunSmoke::GetSpawned(), AGunSmoke::CountLive(),
		AGunSmoke::GetCulled(), AGunSmoke::GetStranded());

	// The muzzle flash, on the same terms: a counted zero rather than an absent
	// line. No cull of its own - a flash lives a tenth of a second, so at most
	// one per gun can be alive and there is nothing for a cap to trim.
	UE_LOG(LogTemp, Display,
		TEXT("FLASHLOG TOTAL spawned=%d live=%d stranded=%d"),
		AMuzzleFlash::GetSpawned(), AMuzzleFlash::CountLive(),
		AMuzzleFlash::GetStranded());

	// The oak thrown out of a hull. spawned counts BURSTS, one per ball that
	// went into a ship, so it can be read straight against the SHOTLOG hit lines
	// - a burst count that drifted from the hit count would mean the effect is
	// firing where no ball landed, or not firing where one did.
	// The scars over the whole level, from the statics: the per-ship HOLELOG
	// lines above are for a human, this is what the suite reads, and it does
	// not lose a wreck that took her instances to the bottom with her.
	UE_LOG(LogTemp, Display,
		TEXT("HOLELOG TOTAL added=%d culled=%d"),
		AShipPawn::GetHolesAddedTotal(), AShipPawn::GetHolesCulledTotal());

	// Play requests, one counter per kind. Held against shots, hull hits,
	// rig hits and splashes by the suite: a gun that goes quiet is a number.
	UE_LOG(LogTemp, Display,
		TEXT("SOUNDLOG TOTAL cannon=%d hit=%d rig=%d splash=%d missing=%d"),
		GetSoundRequests(ESeaSound::Cannon), GetSoundRequests(ESeaSound::Hit),
		GetSoundRequests(ESeaSound::Rig), GetSoundRequests(ESeaSound::Splash),
		GetSoundMissing());

	UE_LOG(LogTemp, Display,
		TEXT("CHIPLOG TOTAL spawned=%d live=%d stranded=%d"),
		AHullSplinters::GetSpawned(), AHullSplinters::CountLive(),
		AHullSplinters::GetStranded());

	// The trail, at quit, whether it was on or off: a counted zero rather than
	// an absent line. stranded is the one that must never move.
	{
		const AShotTrail* Trail = nullptr;
		for (TActorIterator<AShotTrail> It(GetWorld()); It; ++It)
		{
			Trail = *It;
			break;
		}
		UE_LOG(LogTemp, Display,
			TEXT("TRAILLOG TOTAL laid=%d live=%d chains=%d discarded=%d stranded=%d"),
			Trail ? Trail->GetLaid() : 0, Trail ? Trail->GetLive() : 0, Trail ? Trail->GetChains() : 0,
			Trail ? Trail->GetDiscarded() : 0, Trail ? Trail->GetStranded() : 0);
	}

	// THE GUNS AS LAID, printed from the SHIP and not from the picture. The
	// indicator does not exist at all under -AimMarks=0, so a line printed by it
	// would take the train, the elevation and the stop flag down with it - and a
	// pair meant to prove the picture moves nothing would report five missing
	// numbers instead of one moved one. The ship is always there; only `segs`
	// belongs to the marks, and it is the one key that pair is allowed to move.
	{
		const AShipPawn* Laid = nullptr;
		for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
		{
			if (It->IsPlayerControlled())
			{
				Laid = *It;
				break;
			}
		}
		const AAimIndicator* Picture = nullptr;
		for (TActorIterator<AAimIndicator> It(GetWorld()); It; ++It)
		{
			Picture = *It;
			break;
		}
		UE_LOG(LogTemp, Display,
			TEXT("AIMLOG TOTAL hand=%d side=%s train=%+.1f layfwd=%+.3f elev=%.1f ")
			TEXT("fall=%dm stop=%d locked=%d segs=%d"),
			Laid && Laid->IsLayingByHand() ? 1 : 0,
			Laid && Laid->IsLayingStarboard() ? TEXT("starboard") : TEXT("port"),
			Laid ? Laid->GetLayTrainDeg() : 0.f,
			// WHERE THE GUNS REALLY POINT, not what was stored. The stored angle
			// reads +6 on both sides whether or not the guns agree with it, so it
			// cannot see a mirrored sign; this can, and did.
			Laid ? Laid->LayForwardDot() : 0.f,
			Laid ? Laid->GetLayElevationDeg() : 0.f,
			Laid ? (int32)(Laid->RangeForElevationCm(Laid->GetLayElevationDeg()) * 0.01f) : 0,
			Laid && Laid->IsAgainstTheStop() ? 1 : 0,
			Laid && Laid->IsLayLocked() ? 1 : 0,
			Picture ? Picture->GetSegments() : 0);
	}

	// THE BATTERY, as a mask rather than a count, so what the panel WOULD draw
	// is checkable from a headless run - the HUD is never rendered under
	// -NullRHI, so the picture itself cannot be measured. Aftmost gun in bit 0.
	{
		const AShipPawn* Gunner = nullptr;
		for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
		{
			if (It->IsPlayerControlled())
			{
				Gunner = *It;
				break;
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("HUDLOG TOTAL guns_down_port=%d guns_down_stbd=%d ")
			TEXT("guns_ready_port=%d guns_ready_stbd=%d"),
			Gunner ? Gunner->GetGunsDownMask(false) : 0,
			Gunner ? Gunner->GetGunsDownMask(true) : 0,
			// Standing AND loaded, which is what the pips now say and what the
			// player counts before he presses.
			Gunner ? Gunner->GetGunsReady(false) : 0,
			Gunner ? Gunner->GetGunsReady(true) : 0);
	}

	// THE LINE OF BATTLE. How many times a consort had to be stepped over
	// because she had stopped steering - struck, made port, or landed as a
	// prize. Zero in any ordinary action; non-zero exactly when the line closed
	// up, which is the whole point of the fix this counts.
	{
		// THE MAXIMUM, not the sum. Summing the per-ship depths gave 2 where one
		// consort had struck and two ships were following her - which is a count
		// of FOLLOWERS wearing a depth's name, and it would have grown with the
		// squadron rather than with anything tactical. The comment on
		// GetLineSkips said "deepest"; this line said "total". Same class of
		// mistake as the broadside that reported an elevation nobody fired at,
		// and caught the same way: by asking why a number was 2 when the
		// arithmetic said 1.
		int32 Skips = 0;
		for (TActorIterator<AShipAIController> It(GetWorld()); It; ++It)
		{
			Skips = FMath::Max(Skips, It->GetLineSkips());
		}
		UE_LOG(LogTemp, Display, TEXT("AILOG TOTAL line_skips=%d"), Skips);

		// THE LINE, on every row and zeros included. runnerGap: from the first
		// ship that broke off to the nearest consort astern of her that is still
		// fighting - the line having left her behind. stationGap: the widest
		// distance any captain kept station at in the last tick. runnersAfloat:
		// Crown ships running and still afloat - a victory needs every one of
		// them sunk, and a runner with her rig whole is not slowed by her hull.
		const TArray<AShipPawn*> Order = GetOrderOfBattle();
		float RunnerGapM = -1.f;
		int32 RunnersAfloat = 0;
		int32 FirstRunner = INDEX_NONE;
		for (int32 i = 0; i < Order.Num(); ++i)
		{
			if (AShipAIController::IsShipBreakingOff(Order[i]))
			{
				++RunnersAfloat;
				if (FirstRunner == INDEX_NONE)
				{
					FirstRunner = i;
				}
			}
		}
		if (FirstRunner != INDEX_NONE)
		{
			for (int32 i = FirstRunner + 1; i < Order.Num(); ++i)
			{
				if (!Order[i]->IsOutOfTheFight() && !AShipAIController::IsShipBreakingOff(Order[i]))
				{
					const float D = FVector::Dist2D(Order[FirstRunner]->GetActorLocation(),
						Order[i]->GetActorLocation()) * 0.01f;
					RunnerGapM = RunnerGapM < 0.f ? D : FMath::Min(RunnerGapM, D);
				}
			}
		}
		float StationGapM = -1.f;
		for (TActorIterator<AShipAIController> It(GetWorld()); It; ++It)
		{
			if (It->GetLastStationGapCm() >= 0.f)
			{
				StationGapM = FMath::Max(StationGapM, It->GetLastStationGapCm() * 0.01f);
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("LINELOG TOTAL broken=%d closed=%.2f runnerTicks=%d runnerGap=%.0f stationGap=%.0f runnersAfloat=%d"),
			LineBroken, LineClosedAt, LineRunnerTicks, RunnerGapM, StationGapM, RunnersAfloat);
	}

	// ALWAYS, even in a run with no convoy in it: a counted zero. A money
	// counter that is simply absent from thirteen scenarios cannot be told
	// from one that stopped being written.
	UE_LOG(LogTemp, Display,
		TEXT("PRIZELOG PURSE purse=%d prizes=%d valueMax=%d cargo=%d manned=%d refused=%d handsSent=%d closest=%.0f landed=%d landedValue=%d handsHome=%d"),
		Purse, PrizesTaken, PrizeValueMax, ConvoyCargo, PrizesManned,
		PrizesRefused, HandsOutInPrizes, PrizeClosestM, PrizesLanded, Landed,
		HandsHome);
	// What the money BOUGHT, on its own line and always, convoy or not: a
	// counted zero. spent and coffers are printed together because one without
	// the other cannot be told from a ship that had nothing to spend.
	UE_LOG(LogTemp, Display,
		TEXT("PORTLOG REFIT spent=%d coffers=%d handsBought=%d hullBought=%d shotBought=%d refitSeconds=%.1f"),
		Spent, GetCoffers(), HandsBought, GetHullBought(), ShotBought, RefitSeconds);
	// Always, a counted zero: hulls the roadstead would not sell to.
	UE_LOG(LogTemp, Display, TEXT("PORTLOG SIDE refused=%d"), RefusedSide.Num());
	UE_LOG(LogTemp, Display, TEXT("TACKLELOG TOTAL bought=%d refusedCoffers=%d spent=%d"),
		TackleBought, TackleRefusedCoffers, TackleSpent);
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
