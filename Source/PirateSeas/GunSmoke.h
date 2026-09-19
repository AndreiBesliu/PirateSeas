#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GunSmoke.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * The smoke of one gun.
 *
 * A CPU emitter, because Unreal's Python API cannot author a Niagara system and
 * everything in this project is authored from script. That is not the handicap
 * it sounds like: eighteen instanced cards driven from Tick is what a CPU
 * emitter IS, and the project already runs exactly this shape in AOceanSurface -
 * a mesh plus a dynamic material instance it pushes scalars into every frame.
 *
 * One actor per GUN, not per broadside. The ports sit at four stations down the
 * side, so four puffs make a bank of smoke eleven metres long, and that
 * silhouette is what reads as a ship of war rather than as an explosion.
 *
 * Spawned in WORLD SPACE and never attached to the ship. A pall of smoke glued
 * to the hull, travelling with her at six knots, is the single thing that most
 * destroys the illusion: real smoke is left behind.
 *
 * Off by default. -ShipSmoke=1 turns it on. Every gunnery measurement in the
 * project was taken without it, and a default that quietly added thirty-two
 * ticking actors to those runs would invalidate the comparisons they exist for.
 */
UCLASS()
class PIRATESEAS_API AGunSmoke : public AActor
{
	GENERATED_BODY()

public:
	AGunSmoke();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** Spawns one puff at a muzzle. Seed makes the scatter REPRODUCIBLE: it is
	 *  drawn from the shot index rather than from the global stream, so smoke
	 *  cannot perturb the gun scatter that -ShipSeed exists to pin. */
	static AGunSmoke* Spawn(UWorld* World, const FVector& Muzzle,
		const FVector& JetDir, int32 Seed);

	/** THE SMOKE, COUNTED. Static because a puff is an actor and there are many
	 *  of them; read once at quit for the SMOKELOG TOTAL line.
	 *
	 *  GetStranded is the one that must never move: it counts ticks in which a
	 *  puff built its cards with Age already past LifeSeconds, and it sits
	 *  directly after the guard that destroys such a puff. Zero is not evidence
	 *  on its own - the proof is that weakening that guard makes it non-zero. */
	static int32 GetSpawned() { return Spawned; }
	static int32 GetCulled() { return Culled; }
	static int32 GetStranded() { return Stranded; }

	/** Live puffs right now, after dropping the ones the engine has destroyed.
	 *  Not const: it compacts the list, which is the only honest way to answer. */
	static int32 CountLive();

	/** Level start. STRUCTURAL, not field by field: the list and all three
	 *  counters are static, so anything left behind by a previous level survives
	 *  into this one and reads as a measurement rather than as a fault. */
	static void ResetForNewLevel();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Smoke")
	TObjectPtr<UInstancedStaticMeshComponent> Cards;

	/** How long a puff lives. The guns reload in twelve seconds, so one side
	 *  never has two palls alive at once. */
	UPROPERTY(EditAnywhere, Category = "Smoke")
	float LifeSeconds = 9.f;

	/** THE RATIO THAT DECIDES EVERYTHING. A card must be much SMALLER than the
	 *  cloud it belongs to, or the cards cannot arrange themselves into a
	 *  volume - they can only stack into a wall.
	 *
	 *  The first attempt had cards 260 to 400 cm across inside a puff whose
	 *  whole extent was 300 cm: every card was the size of the entire cloud.
	 *  Thirty cards of 130 cm scattered through a ball two metres wide and
	 *  growing is a different object, and no amount of tuning the opacity was
	 *  ever going to turn the first one into the second.
	 */
	UPROPERTY(EditAnywhere, Category = "Smoke")
	int32 CardCount = 30;

	/** Radius of the ball the cards are seeded in, before the jet throws them. */
	UPROPERTY(EditAnywhere, Category = "Smoke")
	float BirthRadiusCm = 130.f;

	/** The jet: the charge throws the smoke out sideways before it becomes a
	 *  cloud. Without it the puff is born as a ball; the jet is what gives it
	 *  an axis, and the axis is what says a gun fired. */
	UPROPERTY(EditAnywhere, Category = "Smoke")
	float JetSeconds = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float JetSpeedMinCmS = 1900.f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float JetSpeedMaxCmS = 4200.f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float JetDragPerSecond = 6.f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float RiseCmS = 90.f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float SpreadCmS = 300.f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float CardGrowthCmS = 62.f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float CardStartCm = 175.f;

	/** The wind takes it to leeward, but not instantly: at full wind from the
	 *  first frame the puff leaves the muzzle before it has formed. */
	UPROPERTY(EditAnywhere, Category = "Smoke")
	float DriftRampSeconds = 2.f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float DriftStartFraction = 0.35f;

	/** Wind shear: the top of the column outruns the bottom, so the puff leans.
	 *  Per metre of height gained. */
	UPROPERTY(EditAnywhere, Category = "Smoke")
	float ShearPerMetre = 0.010f;

	UPROPERTY(EditAnywhere, Category = "Smoke")
	float SpinDegPerSecond = 22.f;

	/** Hard ceiling on live puffs, so a long squadron action cannot fill the
	 *  world with them. Culled oldest first, and the cull is LOGGED - a limit
	 *  that trims silently reads as "there was never more than this". */
	static constexpr int32 MaxLivePuffs = 48;


private:
	struct FCard
	{
		FVector Offset = FVector::ZeroVector;   // relative to the puff centre
		FVector Velocity = FVector::ZeroVector;
		float Spin = 0.f;
		float Roll = 0.f;
		float Size = 0.f;
	};

	TArray<FCard> Deck;
	FVector Centre = FVector::ZeroVector;
	FVector DriftCmS = FVector::ZeroVector;
	float Age = 0.f;
	float BirthZ = 0.f;
	bool bReportedSpread = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SmokeMaterial;

	static TArray<TWeakObjectPtr<AGunSmoke>> Live;
	static int32 Spawned;
	static int32 Culled;
	static int32 Stranded;
};
