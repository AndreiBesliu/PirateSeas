#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShotTrail.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/**
 * The smoke a ball drags behind her, so you can see where your shot went.
 *
 * Asked for by the owner in as many words: "nu se vede foarte bine unde se
 * duc", and then, once he had seen the first attempt, sharpened to "niste linii
 * fumurii care urmeaza ghiulelele si care sunt conice si curbate dupa
 * traiectorie". That second sentence IS the design: a LINE, not a row of dots;
 * TAPERED, not a constant band; and CURVED, which it gets for free by being
 * built out of the flight path itself rather than out of a formula.
 *
 * That makes it LEGIBILITY, not decoration - the graphics arc is parked and this
 * is not part of it. You cannot correct your fire if you cannot see where the
 * last one fell, and until now the only evidence a shot existed was a splash you
 * had to be already looking at.
 *
 * ON EVERY BALL, INCLUDING THE ENEMY'S, by the owner's decision. That changes
 * the GAME and not only the picture: a trail makes incoming fire readable, so a
 * captain can see a broadside coming and put his helm over. The argument for it
 * is that real gunsmoke does exactly that, and the argument against - that it
 * takes away being caught unawares - was put and answered.
 *
 * A RIBBON ON A PROCEDURAL MESH, and the first version was neither. It drew
 * billboard cards through an InstancedStaticMeshComponent, the way AGunSmoke and
 * the wake do, and every colour it was ever given came out BLACK. That was run
 * to ground rather than guessed at: the emissive was proved dead by a five-value
 * tint sweep on ONE binary and ONE material - tint 0 and tint 40 gave pixels
 * identical to the unit.
 *
 * THE CAUSE WAS THE UNIT, and nothing else. This scene's white point is at least
 * 5793 cd/m2 and the cards were authored around 1, so every value tried was zero
 * after exposure. See the Tint comment below for the arithmetic.
 *
 * AND A CLAIM THAT USED TO STAND HERE IS WITHDRAWN. This comment said that the
 * project's InstancedStaticMeshComponent "renders one material, black, and no
 * other", on the strength of putting M_ShipMaster on it and seeing nothing. That
 * test was a mean over a sample box and it could not carry that much weight: the
 * GUN SMOKE now renders correctly through the very same component, once its
 * colours were re-authored in candelas. The component was innocent.
 *
 * The ribbon is still right, for the reason that has nothing to do with any of
 * that: the owner asked for LINES, and a ribbon is what draws a line.
 *
 * ONE ACTOR FOR THE WHOLE WORLD, not one per ball. A trail owned by its ball
 * would die with her, which is precisely the moment you most want to see it: the
 * arc has to still be in the air when the splash goes up, or it answers nothing.
 * So a chain outlives the shot that laid it and fades on its own clock.
 */
UCLASS()
class PIRATESEAS_API AShotTrail : public AActor
{
	GENERATED_BODY()

public:
	AShotTrail();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** Adds a sample to ONE ball's chain, if she has moved far enough since the
	 *  last one. ChainId starts at INDEX_NONE and the trail fills it in, so the
	 *  caller is one line in ACannonBall::Tick and knows nothing about this.
	 *
	 *  Per ball, not per world: four balls of one broadside draw four arcs, and
	 *  sharing one cursor between them would draw a single dotted line with
	 *  three quarters of it missing.
	 *
	 *  Returns false when trails are switched off, so the ball can skip the
	 *  bookkeeping entirely. */
	static bool Lay(UWorld* World, const FVector& Where, int32& ChainId);

	/** The ball is gone. Puts a last sample where she died - so the ribbon
	 *  actually reaches the splash instead of stopping short of it - and closes
	 *  the chain to any more. */
	static void Close(UWorld* World, int32 ChainId, const FVector& Where);

	/** Samples alive right now, across every chain. */
	int32 GetLive() const { return Live; }
	/** Latched, cumulative, for the whole run. */
	int32 GetLaid() const { return Laid; }
	/** Latched: samples thrown away because a chain hit its cap. Not a fault in
	 *  itself - it is the cap doing its job - but a number that should stay at
	 *  zero in an ordinary action, and says so if it does not. */
	int32 GetDiscarded() const { return Discarded; }
	/** LATCHED, and the one that must never move: a sample still in the mesh
	 *  after its life ran out. The wake shipped exactly this defect once - crumbs
	 *  nobody was ageing - and it was invisible because the counter that would
	 *  have caught it was sampled per frame instead of latched. */
	int32 GetStranded() const { return Stranded; }
	/** Chains carrying at least one live sample. Back to zero once every ball
	 *  has landed and every ribbon has faded. */
	int32 GetChains() const { return LiveChains; }

	/** Age of the oldest sample still alive. */
	float GetOldestLive() const { return OldestLive; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Trail")
	TObjectPtr<UProceduralMeshComponent> Ribbon;

	/** How long a piece of ribbon hangs. Long enough that the whole arc is still
	 *  in the air when the splash goes up - a ball crosses four hundred metres in
	 *  about three and a half seconds - and short enough that a fight does not
	 *  end up behind a net of old string. */
	UPROPERTY(EditAnywhere, Category = "Trail")
	float LifeSeconds = 4.5f;

	/** How far the ball must move before another sample is taken. Three metres
	 *  puts about a hundred and thirty on a four-hundred-metre flight, which is
	 *  enough that the ballistic curve reads as a CURVE and not as a folded
	 *  polyline. Sampling per TICK instead would tie the ribbon's shape to the
	 *  frame rate, and this project compares runs of one seed for a living. */
	UPROPERTY(EditAnywhere, Category = "Trail")
	float SpacingCm = 300.f;

	/** THE CONE. Half-width at the head, where the smoke has just left the ball
	 *  and has had no time to spread, and at the tail, where four seconds of air
	 *  have pulled it apart. Narrow to wide going BACKWARDS from the ball, which
	 *  is how powder smoke behaves and how it reads: the point of the cone tells
	 *  you where the shot is now, and the flare behind it tells you where it came
	 *  from. */
	UPROPERTY(EditAnywhere, Category = "Trail")
	float HeadHalfCm = 70.f;

	UPROPERTY(EditAnywhere, Category = "Trail")
	float TailHalfCm = 520.f;

	/** Samples per chain. A four-and-a-half-second life at three metres a sample
	 *  is about a hundred and thirty on a long shot; this leaves room for a
	 *  crowded action without ever growing without bound. */
	UPROPERTY(EditAnywhere, Category = "Trail")
	int32 MaxSamples = 220;

	/** How solid the ribbon is where it is thickest. Pushed into the material AND
	 *  read back with the BOOL the engine returns - the first version threw that
	 *  bool away and printed a readback that was only the material instance
	 *  echoing its own override, which would have looked identical if the
	 *  parameter had never existed. */
	UPROPERTY(EditAnywhere, Category = "Trail")
	float Opacity = 0.45f;

	/** The smoke's colour, in CANDELAS PER SQUARE METRE, which is the unit this
	 *  project's scene actually runs in and the reason a whole session was lost
	 *  to a trail that rendered black. DefaultEngine.ini turns on
	 *  ExtendDefaultLuminanceRange and fences auto-exposure to EV100 12.5-16, so
	 *  the white point is at least 2^12.5 = 5793 cd/m2: an emissive of 2.35
	 *  arrives at four ten-thousandths of white and the filmic toe takes the
	 *  rest. A five-value sweep from 0 to 40 came back pixel-identical, which
	 *  reads exactly like a dead input and is not one.
	 *
	 *  Sunlit powder smoke is 110,000 lux times an albedo near 0.85 over pi,
	 *  which is about 30,000. That is why this number looks absurd and is not. */
	UPROPERTY(EditAnywhere, Category = "Trail")
	FLinearColor Tint = FLinearColor(5000.f, 4900.f, 4700.f, 1.f);

private:
	struct FSample
	{
		FVector Where = FVector::ZeroVector;
		float Age = 0.f;
	};
	struct FChain
	{
		TArray<FSample> Points;   // oldest first
		FVector LastAt = FVector::ZeroVector;
		bool bInUse = false;
		bool bOpen = false;
	};
	TArray<FChain> Chains;

	int32 Live = 0;
	int32 Laid = 0;
	int32 Discarded = 0;
	int32 Stranded = 0;
	int32 LiveChains = 0;
	float LogTimer = 0.f;
	/** The age of the oldest sample still alive, this frame. Not a fault
	 *  detector on its own - a clock that STOPS never crosses any threshold -
	 *  but it is the number that says out loud whether the ribbon is ageing at
	 *  all, and it costs one comparison. */
	float OldestLive = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrailMaterial;

	void Add(int32 ChainId, const FVector& Where);
	int32 OpenChain();
	void Rebuild(const FVector& CamLoc);
	static AShotTrail* Find(UWorld* World);
};
