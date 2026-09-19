// What a hit looks like. Asked for by the owner on 16.09, third in the same
// breath as the smoke and the flash: "o sa vreau si feedback la contact".
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HullSplinters.generated.h"

class UInstancedStaticMeshComponent;

/**
 * A burst of oak thrown out of a hull where a ball went in.
 *
 * It exists because the game had the feedback backwards: a ball that MISSED
 * threw up a column of water anyone could see at three hundred metres, and a
 * ball that HIT produced a log line and a number on a bar. The one outcome the
 * player is trying for was the one with nothing to look at.
 *
 * LIT AND OPAQUE, which is what makes this the cheap member of the family. The
 * gun smoke and the muzzle flash are unlit and translucent, and between them
 * they cost this project a day of authored gradients, a blend mode the engine
 * refused, and the discovery that an emissive under a thousand candelas is
 * simply black. Splinters are wood in daylight: ordinary lit cubes with the
 * hull's own material, no material script at all, and nothing that can be
 * crushed to black by an exposure setting.
 */
UCLASS()
class PIRATESEAS_API AHullSplinters : public AActor
{
	GENERATED_BODY()

public:
	AHullSplinters();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** One burst, at the point the ball went in, thrown back along the way it
	 *  came. Seed keeps the scatter off the global stream that -ShipSeed pins:
	 *  a decoration that draws from it would move every ball fired afterwards. */
	static AHullSplinters* Spawn(UWorld* World, const FVector& Where,
		const FVector& Normal, int32 Seed, const FVector& CarriedVel,
		float SurfaceZ);

	/** THE HITS, COUNTED. spawned is bursts, not splinters: one per ball that
	 *  went into a hull, so it can be read against the SHOTLOG hit lines.
	 *
	 *  GetStranded must never move - it counts ticks in which a burst moved its
	 *  pieces with its age already past its life, and sits directly after the
	 *  guard that destroys such a burst. */
	static int32 GetSpawned() { return Spawned; }
	static int32 GetStranded() { return Stranded; }
	static int32 CountLive();
	static void ResetForNewLevel();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Splinters")
	TObjectPtr<UInstancedStaticMeshComponent> Chips;

	/** Long enough to follow a piece from the hull to the water, short enough
	 *  that a broadside's worth does not litter the sea. */
	UPROPERTY(EditAnywhere, Category = "Splinters")
	float LifeSeconds = 0.9f;

	/** How hard the oak comes out. A ball at four hundred metres a second does
	 *  not nudge splinters loose, it throws them. */
	UPROPERTY(EditAnywhere, Category = "Splinters")
	float SpeedMinCmS = 500.f;

	UPROPERTY(EditAnywhere, Category = "Splinters")
	float SpeedMaxCmS = 1600.f;

	/** Gravity, in the same centimetres a second squared the guns use. */
	UPROPERTY(EditAnywhere, Category = "Splinters")
	float GravityCmS2 = 980.f;

	/** Air, so the smallest pieces stop first and the burst opens out. */
	UPROPERTY(EditAnywhere, Category = "Splinters")
	float DragPerSecond = 1.4f;

	/** How wide the cone off the hull is, in degrees from the surface normal. */
	UPROPERTY(EditAnywhere, Category = "Splinters")
	float ConeDeg = 55.f;

	static constexpr int32 ChipCount = 14;

private:
	struct FChip
	{
		FVector Offset = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector SpinAxis = FVector::UpVector;
		FVector Size = FVector::OneVector;
		float Spin = 0.f;
		float Angle = 0.f;
	};

	/** Where one chip goes. Shared by BeginPlay and Tick so the first frame
	 *  cannot disagree with the rest, and the one place that knows a chip which
	 *  has gone into the sea is not drawn. */
	FTransform ChipTransform(const FChip& C) const;

	TArray<FChip> Deck;
	float Age = 0.f;

	/** The sea's height where the burst happened, sampled once. Waves move, but
	 *  not far in nine tenths of a second, and a per-chip per-frame surface query
	 *  would cost fourteen of them a frame for a thing nobody can see. */
	float WaterZ = -1e9f;

	static TArray<TWeakObjectPtr<AHullSplinters>> Live;
	static int32 Spawned;
	static int32 Stranded;
};
