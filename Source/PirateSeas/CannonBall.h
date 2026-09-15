#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CannonBall.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWaterBodyComponent;

/**
 * A round shot. Physics does the ballistics: the sphere simulates with gravity,
 * so the arc, the drop and the impact all fall out of the engine rather than
 * out of a hand-written trajectory.
 *
 * It dies on the first thing it touches, or when it goes under the sea, or when
 * its fuse of a lifetime runs out.
 */
UCLASS()
class PIRATESEAS_API ACannonBall : public AActor
{
	GENERATED_BODY()

public:
	ACannonBall();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Sets the shot on its way and records where it started, so range can be
	 *  measured when it lands. */
	void Fire(const FVector& Velocity, UWaterBodyComponent* InWater,
		AActor* InShooter, int32 InShotIndex, AActor* InTarget = nullptr);

	/** Damage a direct hit does to a hull. */
	UPROPERTY(EditAnywhere, Category = "Shot")
	float ImpactDamage = 60.f;

	/** Mass of the ball in kilograms. Roughly a 32-pounder. */
	UPROPERTY(EditAnywhere, Category = "Shot")
	float ShotMassKg = 15.f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Shot")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Shot")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse,
		const FHitResult& Hit);

	void ReportAndDie(const TCHAR* Reason, const FVector& Where);

	/** A ball that simply burns its fuse used to end in silence, so the shots
	 *  in a log never added up to the shots fired. */
	virtual void LifeSpanExpired() override;

	UPROPERTY(Transient)
	TObjectPtr<UWaterBodyComponent> Water;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Shooter;

	/** What the gun was laid on, so a miss can be logged as a vector. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Target;

	/** Where the ball was when it was last swept from. The rig is query-only,
	 *  so nothing stops the ball physically: it has to look for itself, along
	 *  the whole path it covered, or at 150 m/s it would step clean through a
	 *  mast between one frame and the next. */
	FVector LastSweepFrom = FVector::ZeroVector;
	bool bSwept = false;

	FVector LaunchLocation = FVector::ZeroVector;
	float FlightTime = 0.f;
	int32 ShotIndex = 0;
	bool bSpent = false;

	/** The wave-surface height under the ball, from the last query. Logged with
	 *  every ending, because it is the one field that tells a flat-plane test
	 *  from a wave-aware one at a glance. */
	float SurfaceZAtDeath = 0.f;

	/** One muzzle-awash line per ball, not per frame. */
	bool bReportedAwash = false;
	/** Said once per ball, not once per frame. */
	bool bWarnedStepOver = false;
};
