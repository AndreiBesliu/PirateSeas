#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Island.generated.h"

class UStaticMeshComponent;

/**
 * Land, and the shallow water round it.
 *
 * The rock is a static mesh that stops round shot and can be looked at, and it
 * does NOT block hulls. That is the whole design, and it was decided by
 * measurement: with a solid block that blocked pawns, a 60-tonne simulating
 * hull driven against it was resolved the only way the solver can resolve
 * penetration, which is upward, onto the top face. 302 contacts, impact normal
 * Z = 1.00, hulls riding to z = 2500 and then sitting there at 0.00 m/s for the
 * remaining 295 seconds of the run. A ship up there has no wet pontoons, so the
 * keel, the sail drive and the rudder all switch themselves off in the same
 * instant and nothing in the project can ever move her again.
 *
 * So a ship never touches rock. She takes the ground on the BANK: a ring of
 * shallow water outside the beach that pushes her back out to sea, harder the
 * further in she stands. The push is offshore and only offshore, so no setting
 * of it can hold a ship who wants to leave - she comes off when the way is
 * taken off her, which is the one thing a square rig can do about it.
 *
 * Two radii describe the island and they are shared with Scripts/island.py,
 * which builds the mesh: ShoreRadius is where the ground reaches the waterline
 * (the script checks that its coastline really is that circle, to a hundredth
 * of a centimetre) and ShoalOuter is where the shelf bottoms out. Everything
 * else is those two numbers times a scale.
 */
UCLASS()
class PIRATESEAS_API AIsland : public AActor
{
	GENERATED_BODY()

public:
	AIsland();

	virtual void BeginPlay() override;

	/** The shore radius the mesh was authored at, in centimetres. Must equal
	 *  R_SHORE in Scripts/island.py. */
	static constexpr float AuthoredShoreCm = 11000.f;

	/** And the outer edge of the shelf: R_SHELF in the same script. */
	static constexpr float AuthoredShoalOuterCm = 16000.f;

	/** How big this island is, as the radius of its shoreline. The mesh and
	 *  both radii scale together from this one number. */
	UPROPERTY(EditAnywhere, Category = "Island")
	float ShoreRadiusCm = AuthoredShoreCm;

	float GetScale() const { return ShoreRadiusCm / AuthoredShoreCm; }
	float GetShoalOuterCm() const { return AuthoredShoalOuterCm * GetScale(); }

	/** The width of the bank: how much shallow water there is between where a
	 *  hull first feels the ground and where the rock begins. A hull that ever
	 *  crosses the whole of it has reached the beach, which the ground force
	 *  is sized to prevent and the ship reports as a warning if it happens. */
	float GetShoalMarginCm() const { return GetShoalOuterCm() - ShoreRadiusCm; }

	/** How far into the bank a point lies, in centimetres, and which way is
	 *  out to sea from it. Zero in deep water, and zero is the ONLY value it
	 *  returns in a world with no island near: the caller takes no branch and
	 *  adds no force, so open water is untouched by construction rather than
	 *  by a small number. OutOffshore is horizontal and unit length. */
	float ShoalPenetrationAt(const FVector& World, FVector& OutOffshore) const;

	/** Where the ground first reaches the waterline: outside this, there is
	 *  water; inside it, there is beach. */
	float GetShoreRadiusCm() const { return ShoreRadiusCm; }

protected:
	/** The land itself: seen, and solid to round shot. */
	UPROPERTY(VisibleAnywhere, Category = "Island")
	TObjectPtr<UStaticMeshComponent> Rock;
};
