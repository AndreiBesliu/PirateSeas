#include "MerchantShipPawn.h"

#include "ShipAIController.h"

AMerchantShipPawn::AMerchantShipPawn()
{
	// Cargo. Set on the class, so there is no tick in which she is anybody's.
	Allegiance = EShipAllegiance::Merchant;
	MaxForwardSpeed *= LadenSpeedFactor;
	// A dozen hands: enough to sail her, not enough to fight her or to knot
	// and splice under fire. Her captain never sends anyone to repair in this
	// slice, so what a broadside breaks stays broken - which is why she
	// strikes.
	HandsMax = 14;
	// No guns, so no magazine. Without this she would carry forty rounds she
	// can never fire and print a SHOT bar's worth of numbers that mean nothing.
	ShotMax = 0;

	AIControllerClass = AShipAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}
