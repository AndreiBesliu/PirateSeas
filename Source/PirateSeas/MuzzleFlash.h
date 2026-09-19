// The short fire at a gun's mouth. Asked for by the owner on 16.09, in the same
// breath as the smoke: "un mic fum care se disipeaza mai greu si imediat la
// tragere un foc scurt".
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MuzzleFlash.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * One flash, at one gun, for a tenth of a second.
 *
 * A near-twin of AGunSmoke and deliberately NOT a shared base class with it: the
 * two agree on how they are born and counted, and disagree on everything that
 * matters. Smoke is matter - it drifts, shears, swells and dies ragged against
 * noise over nine seconds. A flash is light: it points where the gun points, it
 * does not move, and when it stops it stops. Folding them together would have
 * meant a base class of exactly the bookkeeping, and a pair of subclasses that
 * shared nothing else - see the project's note about two implementations that
 * are identical only because one is a bad copy of the other.
 */
UCLASS()
class PIRATESEAS_API AMuzzleFlash : public AActor
{
	GENERATED_BODY()

public:
	AMuzzleFlash();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** Spawns one flash at a muzzle, thrown along that gun's line. Seed makes
	 *  the variation REPRODUCIBLE and, more importantly, keeps it off the global
	 *  stream: -ShipSeed exists so that the gun scatter cannot be perturbed by
	 *  anything drawn afterwards, and a flash drawing two numbers per shot would
	 *  move every ball fired later in the run. */
	static AMuzzleFlash* Spawn(UWorld* World, const FVector& Muzzle,
		const FVector& JetDir, int32 Seed);

	/** THE FLASH, COUNTED - the same four questions the smoke answers, minus the
	 *  cull: a flash lives a tenth of a second, so at most one per gun can be
	 *  alive at once and there is nothing for a cap to trim.
	 *
	 *  GetStranded must never move. It counts ticks in which a flash built its
	 *  cards with its age already past its life, and it sits directly after the
	 *  guard that destroys such a flash - so it can only ever count if that
	 *  guard is weakened, which is how it is proven rather than assumed. */
	static int32 GetSpawned() { return Spawned; }
	static int32 GetStranded() { return Stranded; }

	/** Live flashes right now, after dropping the ones the engine destroyed. */
	static int32 CountLive();

	/** Level start. STRUCTURAL: the list and both counters are static, so what a
	 *  previous level left behind survives into this one and reads as a
	 *  measurement rather than as a fault. */
	static void ResetForNewLevel();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Flash")
	TObjectPtr<UInstancedStaticMeshComponent> Cards;

	/** A tenth of a second. Real powder is gone in twenty to forty
	 *  milliseconds, which at sixty frames is one or two frames - below the
	 *  threshold at which a player can tell a flash from a dropped frame. This
	 *  is six frames: short enough to read as a flash, long enough to be seen at
	 *  all. It is a legibility number, not a physical one, and it is the first
	 *  thing to change if the owner says it reads as a lamp. */
	UPROPERTY(EditAnywhere, Category = "Flash")
	float LifeSeconds = 0.10f;

	/** How far out along the barrel the last card sits. Two metres of burning
	 *  gas off a naval gun is conservative; the muzzle blast of a real
	 *  twenty-four pounder reaches further than the gun is long. */
	UPROPERTY(EditAnywhere, Category = "Flash")
	float ReachCm = 190.f;

	/** The bloom at the mouth, in centimetres across. The cards shrink along the
	 *  jet from this to a third of it, which is what gives a cone from any
	 *  angle out of discs that all face the camera. */
	UPROPERTY(EditAnywhere, Category = "Flash")
	float MouthCm = 130.f;

	static constexpr int32 CardCount = 3;

private:
	struct FCard
	{
		FVector Offset = FVector::ZeroVector;   // along the jet, from the muzzle
		float Size = 0.f;
		float Roll = 0.f;
	};

	TArray<FCard> Deck;
	float Age = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMaterial;

	static TArray<TWeakObjectPtr<AMuzzleFlash>> Live;
	static int32 Spawned;
	static int32 Stranded;
};
