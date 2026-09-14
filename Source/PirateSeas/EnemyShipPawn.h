#pragma once

#include "CoreMinimal.h"
#include "ShipPawn.h"
#include "EnemyShipPawn.generated.h"

/**
 * The same hull, the same rig, the same guns. The only difference is who holds
 * the wheel: this one is possessed by AShipAIController the moment it exists.
 */
UCLASS()
class PIRATESEAS_API AEnemyShipPawn : public AShipPawn
{
	GENERATED_BODY()

public:
	AEnemyShipPawn();
};
