#include "Island.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

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

	// The plants. Hierarchical, so a hundred palms are one draw call and the
	// engine culls them per instance; no collision, because nothing is ever
	// going to walk into a tree in a game played from a quarterdeck, and a
	// hundred collision bodies on a hillside would be paid for nothing.
	Palms = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("Palms"));
	Palms->SetupAttachment(Rock);
	Scrub = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("Scrub"));
	Scrub->SetupAttachment(Rock);
	for (UHierarchicalInstancedStaticMeshComponent* C : { Palms.Get(), Scrub.Get() })
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetMobility(EComponentMobility::Movable);
		// The instances are given world-scaled transforms already; the
		// component must not scale them again with the island's own scale.
		C->SetUsingAbsoluteScale(true);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PalmMesh(
		TEXT("/Game/Meshes/SM_Palm.SM_Palm"));
	if (PalmMesh.Succeeded())
	{
		Palms->SetStaticMesh(PalmMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScrubMesh(
		TEXT("/Game/Meshes/SM_Scrub.SM_Scrub"));
	if (ScrubMesh.Succeeded())
	{
		Scrub->SetStaticMesh(ScrubMesh.Object);
	}
}

void AIsland::ScatterVegetation()
{
	if (!Palms || !Scrub || !Rock || !GetWorld())
	{
		return;
	}

	// The bands, asked for rather than retyped. If the material has no such
	// parameter the fallback is used AND SAID, because a silent fallback here
	// means the plants and the paint are keyed to different numbers and only a
	// screenshot would ever show it.
	float SandTop = 320.f, SandFade = 260.f, RockStart = 0.80f, RockFade = 0.22f;
	int32 Asked = 0, Got = 0;
	if (const UMaterialInterface* M = Rock->GetMaterial(0))
	{
		struct { const TCHAR* Name; float* Out; } Wanted[] = {
			{ TEXT("SandTopCm"), &SandTop }, { TEXT("SandFadeCm"), &SandFade },
			{ TEXT("RockSlopeStart"), &RockStart },
			{ TEXT("RockSlopeFade"), &RockFade } };
		for (auto& W : Wanted)
		{
			++Asked;
			float V = 0.f;
			if (M->GetScalarParameterValue(
					FHashedMaterialParameterInfo(FName(W.Name)), V))
			{
				*W.Out = V;
				++Got;
			}
		}
	}

	// Where turf wins over sand, and ground wins over rock: the half-way point
	// of each of the material's own ramps.
	//
	// NOT SCALED. The material reads ABSOLUTE world Z - SandTopCm and SandFadeCm
	// are centimetres above the waterline, full stop - so scaling the number
	// here moved the plants off the paint on every island that is not exactly
	// the authored size. Measured: at scale 1.27 the plants started 573 cm up
	// while the paint turned to turf at 450, leaving 123 cm of painted grass
	// with nothing growing on it; at 0.55 the plants started at 245 and stood on
	// 205 cm of painted SAND - which is the exact failure that reading the
	// numbers off the material was supposed to make impossible.
	//
	// The other two bounds below KEEP their scale on purpose: they are look
	// choices ("palms on the lower slopes"), not the material's numbers, and a
	// bigger hill should carry them proportionally higher.
	const float Scale = GetScale();
	const float TurfFromCm = SandTop + 0.5f * SandFade;
	const float MinFlatness = RockStart - 0.5f * RockFade;

	const float R = ShoreRadiusCm;
	const FVector Centre = GetActorLocation();
	// Seeded from the island's own placement, so two runs of the same scenario
	// plant the same trees - this project compares runs, and scenery that moved
	// between them would be one more thing to rule out.
	FRandomStream Stream(FMath::RoundToInt(Centre.X + Centre.Y * 7.f) | 1);

	FCollisionQueryParams Q(SCENE_QUERY_STAT(IslandScatter), false, this);
	Q.bTraceComplex = true;
	Q.AddIgnoredActor(this);
	// ...except the island itself, which is what we WANT to hit.
	Q.ClearIgnoredActors();

	int32 Tried = 0, Planted[2] = { 0, 0 }, TooSteep = 0, TooLow = 0, NoHit = 0;
	for (int32 Kind = 0; Kind < 2; ++Kind)
	{
		const float Spacing = (Kind == 0 ? PalmSpacingCm : ScrubSpacingCm) * Scale;
		const float MaxZ = (Kind == 0 ? PalmMaxHeightCm : ScrubMaxHeightCm) * Scale;
		UHierarchicalInstancedStaticMeshComponent* Into = (Kind == 0) ? Palms : Scrub;
		if (!Into->GetStaticMesh())
		{
			continue;
		}
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(2.f * R / Spacing), 1, 200);
		for (int32 iy = -Steps; iy <= Steps; ++iy)
		{
			for (int32 ix = -Steps; ix <= Steps; ++ix)
			{
				// A hexagonal lattice: rows offset by half a step. A square one
				// plants in visible lines the moment the camera is square to it.
				const float X = (ix + ((iy & 1) ? 0.5f : 0.f)) * Spacing
					+ Stream.FRandRange(-0.35f, 0.35f) * Spacing;
				const float Y = iy * Spacing * 0.866f
					+ Stream.FRandRange(-0.35f, 0.35f) * Spacing;
				if (FMath::Square(X) + FMath::Square(Y) > FMath::Square(R))
				{
					continue;
				}
				++Tried;

				const FVector From(Centre.X + X, Centre.Y + Y, Centre.Z + 12000.f);
				const FVector To(From.X, From.Y, Centre.Z - 2000.f);
				FHitResult Hit;
				if (!GetWorld()->LineTraceSingleByChannel(
						Hit, From, To, ECC_Visibility, Q) || Hit.GetActor() != this)
				{
					++NoHit;
					continue;
				}
				const float HeightCm = Hit.ImpactPoint.Z - Centre.Z;
				if (HeightCm < TurfFromCm || HeightCm > MaxZ)
				{
					++TooLow;
					continue;
				}
				if (Hit.ImpactNormal.Z < MinFlatness)
				{
					++TooSteep;
					continue;
				}

				// Stand it up, not normal to the slope: a palm grows towards the
				// light, not square to the hill. A little lean, and a random
				// heading so eight fronds do not point the same way twice.
				const float Lean = Stream.FRandRange(-7.f, 7.f);
				const FRotator Rot(Lean, Stream.FRandRange(0.f, 360.f),
					Stream.FRandRange(-5.f, 5.f));
				const float S = Stream.FRandRange(0.78f, 1.35f) * Scale;
				FTransform T(Rot, Hit.ImpactPoint - Centre, FVector(S, S, S));
				Into->AddInstance(T, /*bWorldSpace=*/false);
				++Planted[Kind];
			}
		}
	}

	// Counted, and every rejection counted separately: "no plants" has four
	// different causes and they need telling apart without a second run.
	UE_LOG(LogTemp, Display,
		TEXT("ISLELOG %s planted palms=%d scrub=%d of %d tried "
			 "(missed=%d, wrong height=%d, too steep=%d); "
			 "turf from %.0f cm (paint says %.0f, scale %.2f), flatness >= %.2f, "
			 "%d of %d numbers read from the material"),
		*GetName(), Planted[0], Planted[1], Tried, NoHit, TooLow, TooSteep,
		TurfFromCm, SandTop + 0.5f * SandFade, Scale, MinFlatness, Got, Asked);
}

void AIsland::BeginPlay()
{
	Super::BeginPlay();

	Rock->SetWorldScale3D(FVector(GetScale()));
	Rock->UpdateBounds();
	ScatterVegetation();

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
