#include "EnemyShipPawn.h"

#include "ShipAIController.h"

AEnemyShipPawn::AEnemyShipPawn()
{
	AIControllerClass = AShipAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}
