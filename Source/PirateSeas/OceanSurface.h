#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OceanSurface.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UWaterBodyComponent;

/**
 * The sea you can see.
 *
 * The Water plugin's own surface never drew in this project: the zone, the
 * body, the info mesh, the materials and every visibility flag all report
 * healthy, and the rendered image is identical with the water mesh switched
 * on and off. So the surface is drawn here instead, and drawn from the SAME
 * wave set the buoyancy reads off the water body, which means the water you
 * watch is the water the hull floats on rather than a decoration that happens
 * to look similar.
 *
 * The mesh is a radial grid: a uniform grid fine enough for a nine-metre wave
 * would need millions of triangles to reach the horizon, so the rings grow
 * geometrically outward from the ship and thirty thousand triangles cover six
 * kilometres with centimetres of detail underfoot.
 */
UCLASS()
class PIRATESEAS_API AOceanSurface : public AActor
{
	GENERATED_BODY()

public:
	AOceanSurface();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Number of Gerstner waves the material can draw. Must match the wave
	 *  count the material was built with, and the generator is set to the
	 *  same number so the two never disagree. */
	static constexpr int32 MaterialWaveCount = 6;

	/** How many islands the material can break surf against. Eight, because
	 *  that is what -Islands= will spawn at most; a ninth is dropped with a
	 *  line in the log rather than silently. */
	static constexpr int32 MaterialIslandCount = 8;

	/** --- the wake ---------------------------------------------------------
	 *
	 *  A ship leaves a trail of white water behind her, and until this the sea
	 *  did not know she was there at all.
	 *
	 *  The memory lives HERE, in C++, as breadcrumbs: each ship drops a point
	 *  every so many metres of travel, and the material draws a capsule chain
	 *  through consecutive points. That is what makes the wake CURVED - put the
	 *  helm over and the new crumbs land where the water actually was, while
	 *  the old ones stay put. A wake computed from the ship's current heading
	 *  would swing the whole two hundred metres of it round like a stick.
	 *
	 *  The obvious alternative is a render target ping-ponged every frame. It
	 *  is more capable and it is the wrong trade here: it puts the state on the
	 *  GPU, where this project's entire instrument - a headless -NullRHI run
	 *  that reads log lines - cannot see it. Every number below is CPU-side and
	 *  can be printed.
	 *
	 *  Three ships, because the parameter budget is real and the wake you
	 *  actually watch is your own and your nearest opponent's. Ships beyond the
	 *  third are COUNTED and logged, not dropped in silence. */
	/** Splashes the material can draw at once. A broadside is four balls and
	 *  two ships can fire together, so eight covers the worst honest case; a
	 *  ninth overwrites the oldest and is COUNTED. */
	static constexpr int32 MaxSplashes = 8;
	/** A round shot has hit the water here. Called by the ball itself, which
	 *  already knows the point exactly - the sea does not have to guess, and
	 *  nothing has to be traced for.
	 *
	 *  Static, and it finds the surface itself, so ACannonBall does not have to
	 *  carry a pointer to something it otherwise knows nothing about. */
	static void ReportSplash(UWorld* World, const FVector& Where);

	static constexpr int32 MaxWakeShips = 3;
	static constexpr int32 CrumbsPerShip = 8;
	static constexpr int32 WakePointCount = MaxWakeShips * CrumbsPerShip;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Sea")
	TObjectPtr<UStaticMeshComponent> Surface;

	/** The grid is re-centred on the ship in steps this big rather than every
	 *  frame, so its triangles do not crawl under the camera. The waves
	 *  themselves are keyed to world position, so the pattern never moves. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	float FollowStepCm = 500.f;

	/** How far up a crest the white water begins, as a fraction of the tallest
	 *  crest this wave set can build. 0.78 means the top fifth breaks, which
	 *  is about what a fresh breeze looks like; lower it and the sea goes
	 *  white, raise it past 1 and it never breaks at all. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	float FoamCrestFraction = 0.78f;

private:
	/** Copies the water body's wave set into the material. */
	void PushWaves();

	/** Copies where the land is into the material, so the sea can break on it.
	 *  Deferred like the waves are: islands are spawned by the game mode and
	 *  may not exist on the surface's first tick. */
	void PushIslands();

	/** Drops breadcrumbs behind whichever ships are being tracked, ages the
	 *  ones already down, and pushes the lot into the material. */
	void UpdateWake(float DeltaSeconds);


	/** Whoever the sea should be centred on. */
	AActor* GetFocus() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SeaMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UWaterBodyComponent> Water;

	bool bWavesPushed = false;
	bool bIslandsPushed = false;

	/** How far a ship travels between breadcrumbs, and how long one lives.
	 *  Eight crumbs at nine metres is seventy-two metres of wake; at six and a
	 *  half metres a second that is eleven seconds of trail, so the lifetime is
	 *  set to match - a crumb that dies before it leaves the chain would put a
	 *  gap in the middle of the wake instead of at its end. */
	UPROPERTY(EditAnywhere, Category = "Wake")
	float WakeSpacingCm = 900.f;

	UPROPERTY(EditAnywhere, Category = "Wake")
	float WakeLifeSeconds = 13.f;

	/** Half-width of the trail, and how much wider it gets with age as the
	 *  wake spreads. */
	UPROPERTY(EditAnywhere, Category = "Wake")
	float WakeHalfWidthCm = 340.f;

	UPROPERTY(EditAnywhere, Category = "Wake")
	float WakeSpreadCmPerSecond = 70.f;

	/** Below this speed a ship leaves nothing: a hull lying still does not make
	 *  white water, and without the gate a stopped ship drops every crumb on
	 *  the same spot and the chain collapses to a dot. */
	UPROPERTY(EditAnywhere, Category = "Wake")
	float WakeMinSpeedCmS = 120.f;

	/** --- what the breadcrumbs cannot draw ---------------------------------
	 *
	 *  The trail is history; these two are the present, and they need the
	 *  ship's live position every frame rather than a point dropped nine metres
	 *  ago.
	 *
	 *  THE COLLAR: white water round the hull itself, where she is pushing the
	 *  sea aside right now. Without it the wake begins in open water with a
	 *  hull floating ahead of it, unconnected.
	 *
	 *  THE BOW ARMS: the two lines of broken water that leave the stem at an
	 *  angle and open out astern. The angle is not a choice - Kelvin's result
	 *  is that a displacement hull's wave pattern sits inside a wedge of about
	 *  nineteen and a half degrees either side of the track, at ANY speed. It
	 *  is the shape everyone recognises from above, and it is why a wake reads
	 *  as a wake rather than as a painted stripe.
	 *
	 *  Laid on the ship's TRACK, not her heading: she makes six to nine degrees
	 *  of leeway, and the water closes behind where she actually went. */
	UPROPERTY(EditAnywhere, Category = "Wake")
	float CollarHalfLengthCm = 1550.f;

	UPROPERTY(EditAnywhere, Category = "Wake")
	float CollarHalfBeamCm = 620.f;

	UPROPERTY(EditAnywhere, Category = "Wake")
	float CollarWidthCm = 220.f;

	/** Kelvin's half-angle. Its tangent is what the material actually needs. */
	UPROPERTY(EditAnywhere, Category = "Wake")
	float BowArmAngleDeg = 19.47f;

	UPROPERTY(EditAnywhere, Category = "Wake")
	float BowArmLengthCm = 5200.f;

	UPROPERTY(EditAnywhere, Category = "Wake")
	float BowArmHalfWidthCm = 130.f;

	/** Speed at which the collar and the arms are at full strength. */
	UPROPERTY(EditAnywhere, Category = "Wake")
	float WakeFullSpeedCmS = 500.f;

	struct FWakeTrail
	{
		TWeakObjectPtr<AActor> Ship;
		/** Newest first. */
		TArray<FVector> Crumbs;
		TArray<float> Ages;
		FVector LastDrop = FVector::ZeroVector;
		bool bHasDropped = false;
	};

	TArray<FWakeTrail> Trails;
	bool bWakeReported = false;

	/** --- splashes ---------------------------------------------------------
	 *
	 *  A ring of white water that opens out and fades. The ball tells the sea
	 *  where it hit; the sea remembers it for a second and a half.
	 *
	 *  Same shape as the wake and the surf before it: state in C++, pushed as
	 *  parameters, drawn in the material. That is now three effects on one
	 *  mechanism, and the reason is the same each time - every number stays on
	 *  the processor where a headless run can print it. */
	UPROPERTY(EditAnywhere, Category = "Splash")
	float SplashLifeSeconds = 1.6f;

	UPROPERTY(EditAnywhere, Category = "Splash")
	float SplashRadiusCm = 900.f;

	struct FSplash
	{
		FVector Where = FVector::ZeroVector;
		float Age = 0.f;
		bool bAlive = false;
	};

	TArray<FSplash> Splashes;
	int32 NextSplash = 0;
	int32 SplashesSeen = 0;
	int32 SplashesOverwritten = 0;
	float RetryTimer = 0.f;
};
