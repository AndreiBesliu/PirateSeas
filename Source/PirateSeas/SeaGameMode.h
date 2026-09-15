#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SeaGameMode.generated.h"

class AShipPawn;

/**
 * Hands out the player's ship, puts the enemy on the water, and turns a
 * sinking into an outcome: defeat and a fresh ship for the player, victory
 * and a fresh enemy for the AI.
 *
 * The enemy is SPAWNED here rather than placed in the level on purpose: a
 * ship spawned once the world is running takes the same path the player's
 * ship does, and that path is the one that is measured.
 */
UCLASS()
class PIRATESEAS_API ASeaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASeaGameMode();

	virtual void BeginPlay() override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;

	int32 GetVictories() const { return Victories; }

	/** How many of them are still afloat. The panel shows it: a player who
	 *  cannot see how much of the squadron is left cannot decide whether to
	 *  press the fight or bear away. */
	int32 CountEnemiesAfloat() const;

	int32 GetSquadronSize() const { return SquadronSize; }

	/** The line of battle, in order, skipping anyone who has struck. The AI
	 *  asks for it rather than scanning the world, and it is rebuilt when a
	 *  ship SINKS rather than when her wreck is finally destroyed: a follower
	 *  holding station on a hull that struck half a minute ago keeps a hole in
	 *  the line behind a ship that no longer steers. */
	TArray<AShipPawn*> GetOrderOfBattle() const;
	int32 GetDefeats() const { return Defeats; }

	/** The convoy, for the panel. Zero merchants means no mission. */
	int32 GetConvoySize() const { return ConvoySize; }
	int32 GetConvoyNeed() const { return ConvoyNeed; }
	int32 GetConvoyStopped() const { return ConvoyStopped; }
	int32 GetConvoyThrough() const { return ConvoyThrough; }
	int32 GetConvoySunk() const { return ConvoySunk; }
	bool IsMissionOver() const { return bMissionOver; }
	const FString& GetMissionResult() const { return MissionResult; }

	/** The purse, for the panel. It is banked at the moment a merchant
	 *  strikes and it buys nothing yet: this slice is what a prize is WORTH,
	 *  not what you do with her. */
	int32 GetPurse() const { return Purse; }
	int32 GetPrizesTaken() const { return PrizesTaken; }
	int32 GetPrizesManned() const { return PrizesManned; }
	int32 GetHandsOutInPrizes() const { return HandsOutInPrizes; }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	TSubclassOf<APawn> EnemyShipClass;

	/** Where the enemy appears, relative to the world origin. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	FVector EnemySpawnLocation = FVector(60000.f, 20000.f, 0.f);

	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float EnemySpawnYaw = 200.f;

	/** How many enemies stand out to meet you. Two is a fight; three put
	 *  fifty-two balls into a player who never moved and never fired, which is
	 *  an execution. -EnemyCount=N overrides it. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	int32 SquadronSize = 2;

	/** How far apart they form up at the start, abeam of one another. They
	 *  fall into line ahead on their own once they are under way. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float SquadronSpacingCm = 15000.f;

	/** Give the world a moment to finish standing up before spawning. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float EnemySpawnDelay = 0.5f;

	/** Awash at ~25 s, hang 5, and a few seconds of plunge put the wreck
	 *  below the clear depth, so the first attempt normally succeeds. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float PlayerRespawnDelay = 35.f;

	/** Time after the LAST of them goes down before a fresh squadron stands
	 *  out. Per-ship respawn would mean a fight that never ends. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float EnemyRespawnDelay = 8.f;

	/** Nothing may stick up above this depth at the spawn point. Compared
	 *  against a hull's whole BOUNDING BOX, not its origin: a wreck plunges
	 *  heeled 35 degrees and pitched 30, so at an origin of -1000 her masts
	 *  are still seven metres out of the water. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float RespawnClearDepthCm = -1000.f;

	/** Only a hull this close to the spawn point delays it. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float RespawnClearRadiusCm = 4000.f;

	/** Once the player's wreck is gone, wait only this long before giving
	 *  them a new ship: the camera has nothing to look at meanwhile. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float WreckGoneRespawnSeconds = 2.f;

	/** Inside the ocean's 5 km collision box: a hull born outside it has no
	 *  water under it. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float OceanHalfExtentCm = 240000.f;

	/** --- the convoy ----------------------------------------------------
	 *
	 *  The first objective. A convoy of merchants is sighted and runs for a
	 *  landfall; it is TAKEN when enough of them have been stopped, and it
	 *  has GOT THROUGH when enough of them are safe or on the bottom that
	 *  that number can no longer be reached. A merchant is stopped when she
	 *  strikes. No prizes, no cargo value, no escort, no boarding: those are
	 *  the next slices, and this one is deliberately only the part that makes
	 *  the SIDE OF THE WIND the decision. */

	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	TSubclassOf<APawn> MerchantShipClass;

	/** How many merchants. -Convoy=N. Zero, the default, is no convoy and no
	 *  mission, and every existing scenario runs exactly as it did. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	int32 ConvoySize = 0;

	/** How many must be stopped for the convoy to be taken. -ConvoyNeed=N;
	 *  zero means half, rounded up. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	int32 ConvoyNeed = 0;

	/** Where the convoy is first sighted. -ConvoyX/-ConvoyY. Well off the
	 *  origin, where the player's hull sits, so a raider's nearest hostile
	 *  hull is a merchant. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	FVector ConvoyStart = FVector(120000.f, 150000.f, 0.f);

	/** Her course, in degrees off where the wind comes FROM. Ninety - a beam
	 *  reach - lays the track ACROSS the wind, so "to windward of the convoy"
	 *  and "to leeward of it" mean the same thing for the whole run and not
	 *  just at the start. -ConvoyWindAngle=. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	float ConvoyWindAngleDeg = 90.f;

	/** How far she has to run to be safe, in metres. -ConvoyRangeM=. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	float ConvoyRangeM = 1500.f;

	/** Inside this of the landfall she is under the fort's guns. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	float LandfallRadiusCm = 15000.f;

	/** Formed abeam, this far apart. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	float ConvoySpacingCm = 15000.f;

	/** What each merchant's hold is worth whole. -ConvoyCargo=N. An integer
	 *  on purpose: an unquoted decimal flag arrives as 0 under PowerShell and
	 *  the run "works" with different numbers. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	int32 ConvoyCargo = 1200;

	/** Men it takes to work a prize. Twelve, which is exactly what the crew
	 *  slice left spare: HandsMax 60 against FullGunCrew 48. So the FIRST
	 *  prize is free at the guns and the second is not - which is the whole
	 *  shape of the decision. -PrizeCrew=N. */
	UPROPERTY(EditDefaultsOnly, Category = "Prize")
	int32 PrizeCrewHands = 12;

	/** Hailing distance: how near a hunter must come for her boats to reach a
	 *  prize. Sized by MEASUREMENT, not by taste - see the log line
	 *  PRIZELOG closest, which is printed in every run whether the doctrine
	 *  is on or off, precisely so this number can be chosen from data.
	 *  -PrizeRangeM=N. */
	UPROPERTY(EditDefaultsOnly, Category = "Prize")
	float PrizeRangeM = 150.f;

	/** How long the boats take, in seconds SPENT within hailing distance.
	 *  ACCUMULATED, not continuous: a rule that reset the moment the taker
	 *  drew off would be satisfied only by a ship that can hold station at a
	 *  distance she has never been asked to hold, and would fail silently by
	 *  never firing at all. -PrizeBoatSeconds=N. */
	UPROPERTY(EditDefaultsOnly, Category = "Prize")
	float PrizeBoatSeconds = 20.f;

	/** How far off the convoy -RaiderSide= puts the raider, in metres, along
	 *  the wind. -RaiderOffingM=. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	float RaiderOffingM = 800.f;

	/** Mirrors the captain's CloseHauledMarginDeg: a bearing this close to
	 *  the eye of the wind, past the no-go angle, is beaten rather than
	 *  sailed. Used only to COUNT beat seconds; it steers nothing. */
	UPROPERTY(EditDefaultsOnly, Category = "Convoy")
	float BeatMarginDeg = 22.f;

private:
	/** Puts a whole squadron on the water, formed abeam. */
	UFUNCTION()
	void SpawnSquadron();

	/** One ship of it, on a given station. Returns her, or null. */
	AShipPawn* SpawnOneEnemy(const FVector& Where, const FRotator& Heading, int32 Station);

	/** Land, behind -Islands=N. Spawned, like everything else that has to
	 *  exist once the world is running. */
	void SpawnIslands();

	/** Everything in the level whose object type is WorldStatic, with its
	 *  bounds. Read BEFORE any shot number is believed: the ball's sweep now
	 *  queries that channel, so if anything but the island answers to it, every
	 *  ball could be dying at the muzzle and the island's own counter would
	 *  report a triumphant zero. */
	void DumpWorldStaticCensus() const;

	/** The visible sea. Spawned rather than placed, like everything else that
	 *  has to exist after the world is running. */
	void SpawnOceanSurface();

	/** Sets the sea state on every water body. Runs BEFORE the surface is
	 *  spawned and before any hull registers, so the waves the surface draws
	 *  and the waves the hulls feel are the same waves. */
	void SetSeaState();

	/** Puts the sun where that hour of the day would put it, and takes the
	 *  colour, the strength, the sky and the exposure band with it.
	 *
	 *  OPT-IN. Without -Hour= nothing here runs and the level keeps the light it
	 *  was authored with, so the shipped look and every baselined number are
	 *  exactly as they were. A time of day that quietly moved them would make
	 *  this a lighting change pretending to be a feature. */
	void SetTimeOfDay();

	/** Hours since midnight, 0 to 24. Negative means "leave the level alone",
	 *  which is the default and what every scenario but the new one uses. */
	UPROPERTY(EditDefaultsOnly, Category = "Sky")
	float HourOfDay = -1.f;

	/** How high the sun gets at noon, in degrees. Sixty-two is a low-latitude
	 *  summer; the Caribbean this is meant to evoke is about that. */
	UPROPERTY(EditDefaultsOnly, Category = "Sky")
	float NoonElevationDeg = 62.f;

	/** Sunrise and sunset, in hours. The day is stretched between them. */
	UPROPERTY(EditDefaultsOnly, Category = "Sky")
	float SunriseHour = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "Sky")
	float SunsetHour = 18.f;

	/** Number of Gerstner waves in the swell. Six, because the sea material
	 *  draws six: any more would be felt by the hulls and not seen. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	int32 SwellWaveCount = 6;

	/** Smallest and largest wave, in centimetres of amplitude. The map came
	 *  with a five-metre swell, which is heavy weather for a 37 m hull. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float SwellMinAmplitudeCm = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float SwellMaxAmplitudeCm = 32.f;

	/** How far off the wind the wave train is allowed to fan, in degrees. The
	 *  engine rotates every wave after the first by a random angle in
	 *  [-spread, +spread], so anything at or over 180 is a sea with no
	 *  direction: this was 400. */
	UPROPERTY(EditDefaultsOnly, Category = "Sea")
	float SwellSpreadDeg = 45.f;

	void BindShip(AShipPawn* Ship);
	void HandleShipSunk(AShipPawn* Ship, AActor* Causer);
	void HandleShipWrecked(AShipPawn* Ship);

	UFUNCTION()
	void TryRespawnPlayer();
	UFUNCTION()
	void TryRespawnEnemy();
	UFUNCTION()
	void QuitNow();
	UFUNCTION()
	void DumpWaterRenderingLater();
	FTimerHandle WaterDumpTimer;

	UFUNCTION()
	void ScuttlePlayerForTest();
	UFUNCTION()
	void ScuttleEnemyForTest();

	/** What the physics scene actually sees of the sea. */
	void DumpOceanCollision() const;

	/** Every gate between "there is an ocean in the level" and "there are
	 *  triangles on screen". The sea has never drawn; this says which gate. */
	void DumpWaterRendering() const;

	/** Reads -Convoy= and its flags and lays the course, BEFORE the islands
	 *  are placed, so every station and the landfall itself are on the
	 *  island's refusal list. -RaiderSide= is read here too, because it moves
	 *  the squadron's spawn and that has to be known for the same reason. */
	void ReadConvoyFlags();

	UFUNCTION()
	void SpawnConvoy();

	/** Once a second: who has the weather gauge, is the raider beating, and
	 *  has anyone made port. Latched into cumulative counters, because a
	 *  count of a thing that must happen a certain way is worthless if it is
	 *  sampled and printed. */
	UFUNCTION()
	void SampleWeatherGauge();

	/** Twice a second, on its OWN timer, which is never cleared - unlike the
	 *  weather gauge, which stops at the end of the mission. A prize can be
	 *  manned after the convoy is decided, and a mission that is over is not
	 *  a sea that is empty. */
	UFUNCTION()
	void SamplePrizes();

	/** -ConvoyStrikeTest=N: the first merchant still running strikes at N
	 *  seconds, through Strike() and nothing else, so the whole path from a
	 *  strike to a finished mission can be proved without a single shot. */
	UFUNCTION()
	void StrikeMerchantForTest();

	void HandleShipStruck(AShipPawn* Ship, AActor* Causer);

	/** Idempotent. Called from every exit - taken, got through, and the
	 *  quit timer for a run that ended neither way - so exactly one MISSION
	 *  line is printed per run, whatever happened. */
	void FinishMission(const TCHAR* Result);

	/** Who the counters follow: the first ship of the squadron when
	 *  -RaiderSide= placed one, otherwise the player. */
	AShipPawn* GetRaider() const;

	AShipPawn* NearestMerchantInTheFight(const FVector& From) const;

	/** True when no hull, afloat or sinking, still stands above the water
	 *  near that point. Checks EVERY ship, not just the one that sank: two
	 *  60-tonne boxes born inside each other are thrown apart by the solver. */
	bool IsSpawnClear(const FVector& Where, const AShipPawn* Ignore) const;

	TWeakObjectPtr<AShipPawn> PlayerShip;
	/** Every enemy currently on the water. An array, not one pointer: with a
	 *  single pointer the game mode only ever knew about whichever ship was
	 *  spawned last, so sinking one of three respawned all three. */
	TArray<TWeakObjectPtr<AShipPawn>> Squadron;
	FTransform PlayerSpawnTransform;
	bool bPlayerSpawnRecorded = false;
	int32 Victories = 0;
	int32 Defeats = 0;

	FTimerHandle EnemySpawnTimer;
	FTimerHandle PlayerRespawnTimer;
	FTimerHandle EnemyRespawnTimer;
	FTimerHandle QuitTimer;
	FTimerHandle SinkTestTimer;
	FTimerHandle EnemySinkTestTimer;

	/** -ShipQuitAfter=N, on world time, so a run ends even after the player's
	 *  ship is a wreck. */
	/** Retries before a blocked spawn gives up and stands out anyway. */
	static constexpr int32 MaxRespawnAttempts = 30;
	int32 EnemyRespawnAttempts = 0;
	int32 PlayerRespawnAttempts = 0;

	float QuitAfterSeconds = 0.f;
	/** -ShipSinkTest=N / -EnemySinkTest=N scuttle one ship at N seconds;
	 *  -ShipSinkSide=port|starboard picks the breach side. */
	float ShipSinkTestAt = 0.f;
	float EnemySinkTestAt = 0.f;
	bool bSinkTestStarboard = true;

	TArray<FVector> ConvoyStations;
	FVector Landfall = FVector::ZeroVector;
	float ConvoyCourseYaw = 0.f;
	TArray<TWeakObjectPtr<AShipPawn>> Convoy;
	/** "weather", "lee", or empty when nobody was placed. */
	FString RaiderSide;
	/** The money, all latched. Purse is the sum of the prize values banked;
	 *  PrizeValueMax is the best single prize, which is the key the pair
	 *  turns on. */
	int32 Purse = 0;
	int32 PrizesTaken = 0;
	int32 PrizeValueMax = 0;
	/** Possession, all latched. Mirrored HERE rather than read off the raider
	 *  at quit, because a wrecked raider is destroyed and would take her
	 *  counters down with her. */
	int32 PrizesManned = 0;
	int32 PrizesRefused = 0;
	int32 HandsOutInPrizes = 0;
	/** Closest any hunter came to a struck prize, in metres, over the run.
	 *  Written whether the doctrine is on or off: it is how the hailing
	 *  distance was chosen, and how a take that never happens explains
	 *  itself. */
	float PrizeClosestM = -1.f;
	FTimerHandle PrizeTimer;
	/** Seconds each prize has had a hunter within hail. */
	TMap<TWeakObjectPtr<AShipPawn>, float> PrizeBoatTime;
	/** Prizes whose refusal has already been said once. */
	TSet<TWeakObjectPtr<AShipPawn>> RefusedPrizes;
	/** Latched, never sampled. */
	int32 ConvoyStopped = 0;
	int32 ConvoyThrough = 0;
	int32 ConvoySunk = 0;
	int32 GaugeTicks = 0;
	int32 LeeTicks = 0;
	int32 BeatSeconds = 0;
	float FirstStrikeAt = -1.f;
	bool bMissionOver = false;
	bool bRaiderLogged = false;
	FString MissionResult;
	float ConvoyStrikeTestAt = 0.f;
	FTimerHandle ConvoySpawnTimer;
	FTimerHandle GaugeTimer;
	FTimerHandle ConvoyStrikeTestTimer;
};
