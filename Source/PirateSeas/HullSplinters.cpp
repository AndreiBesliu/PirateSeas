#include "HullSplinters.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

TArray<TWeakObjectPtr<AHullSplinters>> AHullSplinters::Live;
int32 AHullSplinters::Spawned = 0;
int32 AHullSplinters::Stranded = 0;

int32 AHullSplinters::CountLive()
{
	Live.RemoveAll([](const TWeakObjectPtr<AHullSplinters>& P) { return !P.IsValid(); });
	return Live.Num();
}

void AHullSplinters::ResetForNewLevel()
{
	Live.Reset();
	Spawned = 0;
	Stranded = 0;
}

AHullSplinters::AHullSplinters()
{
	PrimaryActorTick.bCanEverTick = true;

	Chips = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Chips"));
	RootComponent = Chips;
	Chips->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Chips->SetGenerateOverlapEvents(false);
	Chips->SetCastShadow(false);
	Chips->bReceivesDecals = false;
	Chips->SetMobility(EComponentMobility::Movable);
	Chips->SetCullDistance(0);

	// A CUBE, not a card. Splinters are chunky and they tumble; a flat quad
	// spinning about its own plane vanishes twice a revolution, which reads as
	// flicker rather than as wood.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Chips->SetStaticMesh(Cube.Object);
	}
	// The ship's own timber, so the pieces are the colour of the thing they came
	// out of, under the same sun, with no new asset to author or keep in step.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Wood(
		TEXT("/Game/Materials/MI_Wood.MI_Wood"));
	if (Wood.Succeeded())
	{
		Chips->SetMaterial(0, Wood.Object);
	}
}

AHullSplinters* AHullSplinters::Spawn(UWorld* World, const FVector& Where,
	const FVector& Normal, int32 Seed, const FVector& CarriedVel, float SurfaceZ)
{
	if (!World)
	{
		return nullptr;
	}

	Live.RemoveAll([](const TWeakObjectPtr<AHullSplinters>& P) { return !P.IsValid(); });

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AHullSplinters* Burst = World->SpawnActorDeferred<AHullSplinters>(
		AHullSplinters::StaticClass(), FTransform(Where), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Burst)
	{
		return nullptr;
	}

	// Its own stream, seeded from the shot index: the gun loop draws off the one
	// -ShipSeed pins, and anything drawn from that afterwards moves every ball
	// fired later in the run.
	FRandomStream Rng(Seed * 6421 + 97);

	// OUT OF THE HULL, not out of nowhere. The normal the hit reported points
	// away from the timber, so the cone is built around it; a burst thrown along
	// the ball's own direction would be pieces going INTO the ship.
	FVector Out = Normal.GetSafeNormal();
	if (Out.IsNearlyZero())
	{
		Out = FVector::UpVector;
	}

	Burst->Deck.Reset(ChipCount);
	for (int32 i = 0; i < ChipCount; ++i)
	{
		FChip C;
		// A cone about the normal, widened by ConeDeg. Two angles rather than a
		// random unit vector rejected into a cone: the rejection loop's number of
		// draws depends on the numbers it draws, which is exactly the kind of
		// thing that makes a seeded run stop being reproducible.
		const float Yaw = Rng.FRandRange(0.f, 360.f);
		const float Pitch = Rng.FRandRange(0.f, Burst->ConeDeg);
		const FVector Side = FVector::CrossProduct(Out,
			FMath::Abs(Out.Z) > 0.9f ? FVector::ForwardVector : FVector::UpVector)
			.GetSafeNormal();
		FVector Dir = Out.RotateAngleAxis(Pitch, Side);
		Dir = Dir.RotateAngleAxis(Yaw, Out);

		// PLUS THE SHIP'S WAY. The burst is a free actor in world space, so
		// without this the oak came out of a hull doing six metres a second and
		// then stood still while the hole sailed out from under it - the pieces
		// streamed astern of the ship they came from.
		C.Velocity = Dir * Rng.FRandRange(Burst->SpeedMinCmS, Burst->SpeedMaxCmS)
			+ CarriedVel;
		C.Offset = Dir * Rng.FRandRange(0.f, 25.f);
		// Long and thin, in centimetres. The engine cube is 100 cm, so these are
		// fractions of it.
		C.Size = FVector(Rng.FRandRange(0.14f, 0.30f),
			Rng.FRandRange(0.02f, 0.05f), Rng.FRandRange(0.02f, 0.06f));
		C.SpinAxis = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
			Rng.FRandRange(-1.f, 1.f)).GetSafeNormal();
		C.Spin = Rng.FRandRange(360.f, 1400.f);
		Burst->Deck.Add(C);
	}
	Burst->WaterZ = SurfaceZ;
	Burst->FinishSpawning(FTransform(Where));
	Live.Add(Burst);
	++Spawned;
	return Burst;
}

FTransform AHullSplinters::ChipTransform(const FChip& C) const
{
	// GONE UNDER, drawn at nothing. The pieces fall for the better part of a
	// second and the sea is right there; without this they carried on tumbling
	// below it, and the ones that had not yet reached it all vanished together
	// when the actor died. An opaque cube cannot fade, so the honest end for a
	// splinter is the water.
	if (C.Offset.Z + GetActorLocation().Z <= WaterZ)
	{
		return FTransform(FQuat::Identity, C.Offset, FVector::ZeroVector);
	}
	const FQuat Tumble(C.SpinAxis, FMath::DegreesToRadians(C.Angle));
	return FTransform(Tumble, C.Offset, C.Size);
}

void AHullSplinters::BeginPlay()
{
	Super::BeginPlay();

	if (Chips)
	{
		// AT THEIR REAL TRANSFORMS. AddInstance(Identity) carries scale (1,1,1),
		// and the engine cube is 100 cm - so every burst opened with one frame of
		// fourteen coincident ONE-METRE oak blocks hanging at the hit point,
		// before Tick shrank them to splinters on the frame after. On a hitched
		// frame that was the only thing the burst ever drew.
		Chips->ClearInstances();
		for (const FChip& C : Deck)
		{
			Chips->AddInstance(ChipTransform(C), false);
		}
	}
}

void AHullSplinters::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	if (Age >= LifeSeconds)
	{
		Destroy();
		return;
	}

	// MUST NEVER FIRE. Directly after the guard above, so the only way to reach
	// it is to weaken that guard - which is how this counter is proven to work
	// rather than being a zero nobody can move.
	if (Age >= LifeSeconds)
	{
		++Stranded;
	}

	TArray<FTransform> Xforms;
	Xforms.Reserve(Deck.Num());
	for (FChip& C : Deck)
	{
		C.Velocity.Z -= GravityCmS2 * DeltaSeconds;
		C.Velocity *= FMath::Exp(-DragPerSecond * DeltaSeconds);
		C.Offset += C.Velocity * DeltaSeconds;
		C.Angle += C.Spin * DeltaSeconds;

		Xforms.Add(ChipTransform(C));
	}

	if (Chips && Xforms.Num() == Chips->GetInstanceCount())
	{
		Chips->BatchUpdateInstancesTransforms(0, Xforms, false, true, false);
	}
}
