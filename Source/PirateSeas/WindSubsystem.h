#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WindSubsystem.generated.h"

/**
 * World-wide wind. One source of truth so every ship, and later every sail
 * effect, reads the same weather.
 *
 * Direction and strength drift slowly from layered sine waves rather than a
 * random walk, so a replay of the same timestamps gives the same weather.
 */
UCLASS()
class PIRATESEAS_API UWindSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Unit vector the wind blows TOWARDS, in world space. */
	UFUNCTION(BlueprintPure, Category = "Wind")
	FVector GetWindDirection() const;

	/** Compass bearing the wind blows towards, in degrees. */
	UFUNCTION(BlueprintPure, Category = "Wind")
	float GetWindBearingDeg() const { return CurrentBearingDeg; }

	/** Wind speed in metres per second. */
	UFUNCTION(BlueprintPure, Category = "Wind")
	float GetWindSpeedMS() const { return CurrentSpeedMS; }

	/** Overrides the drift and pins the bearing. Used by the sweep diagnostic. */
	void SetForcedBearing(float BearingDeg);
	void ClearForcedBearing() { bBearingForced = false; }

	/** And the strength. Pinning the bearing alone is not enough to measure a
	 *  polar: the gusts swing the speed by a third either way on periods of two
	 *  and five minutes, so two passes over the same wind angle taken minutes
	 *  apart are taken in different weather. Measured without this, the same
	 *  hull at the same angle reported drive 0.655 on one tack and 1.040 on
	 *  the other, and the difference was entirely the gust. */
	void SetForcedSpeed(float SpeedMS);
	void ClearForcedSpeed() { bSpeedForced = false; }

	/** Mean wind, in metres per second. A fresh breeze by default. */
	UPROPERTY(EditAnywhere, Category = "Wind")
	float MeanSpeedMS = 9.f;

	/** How far the speed swings either side of the mean. */
	UPROPERTY(EditAnywhere, Category = "Wind")
	float SpeedVariationMS = 3.f;

	/** How far the bearing wanders either side of its base, in degrees. */
	UPROPERTY(EditAnywhere, Category = "Wind")
	float BearingWanderDeg = 22.f;

	/** Base bearing the weather sits around. */
	UPROPERTY(EditAnywhere, Category = "Wind")
	float BaseBearingDeg = 35.f;

private:
	float ElapsedTime = 0.f;
	float CurrentBearingDeg = 35.f;
	float CurrentSpeedMS = 9.f;
	bool bBearingForced = false;
	float ForcedBearingDeg = 0.f;
	bool bSpeedForced = false;
	float ForcedSpeedMS = 0.f;
};
