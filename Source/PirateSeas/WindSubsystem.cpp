#include "WindSubsystem.h"

void UWindSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CurrentBearingDeg = BaseBearingDeg;
	CurrentSpeedMS = MeanSpeedMS;
}

TStatId UWindSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWindSubsystem, STATGROUP_Tickables);
}

void UWindSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ElapsedTime += DeltaTime;

	if (bBearingForced)
	{
		CurrentBearingDeg = ForcedBearingDeg;
	}
	else
	{
		// Two slow sines of unrelated periods: the wind wanders without ever
		// repeating a short, obvious cycle.
		const float Wander =
			FMath::Sin(ElapsedTime * 0.031f) * 0.65f +
			FMath::Sin(ElapsedTime * 0.011f) * 0.35f;
		CurrentBearingDeg = BaseBearingDeg + Wander * BearingWanderDeg;
	}

	if (bSpeedForced)
	{
		CurrentSpeedMS = ForcedSpeedMS;
	}
	else
	{
		const float Gust =
			FMath::Sin(ElapsedTime * 0.047f) * 0.6f +
			FMath::Sin(ElapsedTime * 0.019f) * 0.4f;
		CurrentSpeedMS = FMath::Max(0.f, MeanSpeedMS + Gust * SpeedVariationMS);
	}
}

FVector UWindSubsystem::GetWindDirection() const
{
	const float Rad = FMath::DegreesToRadians(CurrentBearingDeg);
	return FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
}

void UWindSubsystem::SetForcedBearing(float BearingDeg)
{
	bBearingForced = true;
	ForcedBearingDeg = BearingDeg;
	CurrentBearingDeg = BearingDeg;
}

void UWindSubsystem::SetForcedSpeed(float SpeedMS)
{
	bSpeedForced = true;
	ForcedSpeedMS = FMath::Max(0.f, SpeedMS);
	CurrentSpeedMS = ForcedSpeedMS;
}
