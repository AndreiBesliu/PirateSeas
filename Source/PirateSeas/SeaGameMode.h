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
};
