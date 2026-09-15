#include "MerchantShipPawn.h"

#include "ShipAIController.h"

AMerchantShipPawn::AMerchantShipPawn()
{
	// Cargo. Set on the class, so there is no tick in which she is anybody's.
	Allegiance = EShipAllegiance::Merchant;
	MaxForwardSpeed *= LadenSpeedFactor;

	AIControllerClass = AShipAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}
