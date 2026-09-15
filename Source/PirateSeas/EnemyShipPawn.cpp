#include "EnemyShipPawn.h"

#include "ShipAIController.h"

AEnemyShipPawn::AEnemyShipPawn()
{
	// The Crown's. Set on the class, so every enemy hull ever spawned has a side
	// before its first tick.
	Allegiance = EShipAllegiance::Crown;

	AIControllerClass = AShipAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}
