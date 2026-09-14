#include "Island.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AIsland::AIsland()
{
	PrimaryActorTick.bCanEverTick = false;

	Rock = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rock"));
	SetRootComponent(Rock);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> IslandMesh(
		TEXT("/Game/Meshes/SM_Island.SM_Island"));
	if (IslandMesh.Succeeded())
	{
		Rock->SetStaticMesh(IslandMesh.Object);
	}

	Rock->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Rock->SetCollisionObjectType(ECC_WorldStatic);
	Rock->SetCollisionResponseToAllChannels(ECR_Block);
	// The one line the whole slice rests on. A hull must never be resolved
	// against this surface; she is stopped by the bank outside it instead.
	// Done here, on the island, rather than by changing the hull's response to
	// WorldStatic: the hull's channels were paid for (Overlap on WorldDynamic
	// is the reason the ships float at all) and a change there is global.
	Rock->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// Movable although it never moves. A Static component refuses to be placed
	// or scaled once the world is running: the first probe logged an island at
	// a position the physics scene had never heard of, and ships sailed 148 m
	// through solid rock without one contact.
	Rock->SetMobility(EComponentMobility::Movable);
	Rock->SetSimulatePhysics(false);
	Rock->SetGenerateOverlapEvents(false);
}

void AIsland::BeginPlay()
{
	Super::BeginPlay();

	Rock->SetWorldScale3D(FVector(GetScale()));

	// What the scene holds, not what was asked for. Asking is what lied the
	// first two times.
	const FBoxSphereBounds B = Rock->Bounds;
	UE_LOG(LogTemp, Display,
		TEXT("ISLELOG island at (%.0f,%.0f) shore=%.0f shoalOuter=%.0f margin=%.0f scale=%.2f | scene: mesh=%s centre=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f) enabled=%d objType=%d respPawn=%d mobility=%d"),
		GetActorLocation().X, GetActorLocation().Y,
		ShoreRadiusCm, GetShoalOuterCm(), GetShoalMarginCm(), GetScale(),
		Rock->GetStaticMesh() ? *Rock->GetStaticMesh()->GetName() : TEXT("NONE"),
		B.Origin.X, B.Origin.Y, B.Origin.Z,
		B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z,
		(int32)Rock->GetCollisionEnabled(),
		(int32)Rock->GetCollisionObjectType(),
		(int32)Rock->GetCollisionResponseToChannel(ECC_Pawn),
		(int32)Rock->Mobility);

	if (!Rock->GetStaticMesh())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ISLELOG island has NO MESH: it will stop no shot and be seen by nobody"));
	}
}

float AIsland::ShoalPenetrationAt(const FVector& World, FVector& OutOffshore) const
{
	FVector Out = World - GetActorLocation();
	Out.Z = 0.f;
	const float Dist = Out.Size();

	if (Dist < 1.f)
	{
		// Dead over the middle of the island, which cannot happen while the
		// bank works, but a zero-length normal is a silent no-force rather
		// than an error, so it gets a direction anyway.
		OutOffshore = FVector(1.f, 0.f, 0.f);
		return GetShoalOuterCm();
	}

	OutOffshore = Out / Dist;
	return FMath::Max(0.f, GetShoalOuterCm() - Dist);
}
