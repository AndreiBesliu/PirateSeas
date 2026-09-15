#pragma once

#include "CoreMinimal.h"
#include "ShipPawn.h"
#include "MerchantShipPawn.generated.h"

/**
 * The same hull again, on the third side. She is driven by the same
 * AShipAIController as the enemy, which reads her allegiance and sails her a
 * laid course to a landfall instead of fighting: she has no quarrel with
 * anyone and no way to pursue one. What makes her a merchant is what happens
 * when she is hurt - she STRIKES, where a fighting ship would sink.
 */
UCLASS()
class PIRATESEAS_API AMerchantShipPawn : public AShipPawn
{
	GENERATED_BODY()

public:
	AMerchantShipPawn();

protected:
	/** What a hold full of cargo does to the same hull: her top speed is
	 *  this fraction of a fighting ship's. Measured before it existed, with
	 *  the merchant under HALF canvas instead: she still made 5.2 m/s against
	 *  the raider's 6.5, because drive falls off as 1 - v/Vmax and the sea
	 *  gives most of it back - a tail chase at 1.3 m/s of overhaul, three
	 *  hundred seconds to close 350 m, and one broadside in the whole run.
	 *  Canvas is not the knob; the waterline is. 0.55 puts a laden merchant
	 *  at about seven knots against the raider's twelve, which is what the
	 *  period says and what makes the chase a chase. */
	UPROPERTY(EditDefaultsOnly, Category = "Ship")
	float LadenSpeedFactor = 0.55f;
};
