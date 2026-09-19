#include "MuzzleFlash.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

TArray<TWeakObjectPtr<AMuzzleFlash>> AMuzzleFlash::Live;
int32 AMuzzleFlash::Spawned = 0;
int32 AMuzzleFlash::Stranded = 0;

int32 AMuzzleFlash::CountLive()
{
	Live.RemoveAll([](const TWeakObjectPtr<AMuzzleFlash>& P) { return !P.IsValid(); });
	return Live.Num();
}

void AMuzzleFlash::ResetForNewLevel()
{
	Live.Reset();
	Spawned = 0;
	Stranded = 0;
}

AMuzzleFlash::AMuzzleFlash()
{
	PrimaryActorTick.bCanEverTick = true;

	Cards = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Cards"));
	RootComponent = Cards;
	Cards->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cards->SetGenerateOverlapEvents(false);
	Cards->SetCastShadow(false);
	Cards->bReceivesDecals = false;
	Cards->SetMobility(EComponentMobility::Movable);
	Cards->SetCullDistance(0);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane.Succeeded())
	{
		Cards->SetStaticMesh(Plane.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlashMat(
		TEXT("/Game/Materials/M_MuzzleFlash.M_MuzzleFlash"));
	if (FlashMat.Succeeded())
	{
		Cards->SetMaterial(0, FlashMat.Object);
	}
}

AMuzzleFlash* AMuzzleFlash::Spawn(UWorld* World, const FVector& Muzzle,
	const FVector& JetDir, int32 Seed)
{
	if (!World)
	{
		return nullptr;
	}

	Live.RemoveAll([](const TWeakObjectPtr<AMuzzleFlash>& P) { return !P.IsValid(); });

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMuzzleFlash* Flash = World->SpawnActorDeferred<AMuzzleFlash>(
		AMuzzleFlash::StaticClass(), FTransform(Muzzle), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Flash)
	{
		return nullptr;
	}

	// ITS OWN STREAM. FRandomStream and not FMath::FRandRange, for the same
	// reason the smoke uses one: the gun loop draws two numbers per mounted gun
	// off the global stream that -ShipSeed pins, and anything drawn from that
	// stream afterwards moves every ball fired later in the run. A flash is
	// decoration; it must not be able to move a splash.
	FRandomStream Rng(Seed * 7919 + 13);

	const FVector Jet = JetDir.GetSafeNormal();
	Flash->Deck.Reset(CardCount);
	for (int32 i = 0; i < CardCount; ++i)
	{
		// Along the barrel, shrinking. Discs that all face the camera would read
		// as a blob at any one size; three of them strung out along the jet and
		// falling from the mouth's width to a third of it read as a cone from
		// every angle, without a single card being oriented.
		// CardCount - 1 is a divisor, and CardCount is a tunable sitting in the
		// header beside LifeSeconds and ReachCm. At 1 this divided by zero and
		// filled the instance buffer with NaN.
		const float T = (CardCount > 1)
			? static_cast<float>(i) / static_cast<float>(CardCount - 1) : 0.f;
		FCard C;
		C.Offset = Jet * (Flash->ReachCm * T);
		C.Size = Flash->MouthCm * FMath::Lerp(1.f, 0.33f, T)
			* Rng.FRandRange(0.88f, 1.12f);
		C.Roll = Rng.FRandRange(0.f, 2.f * PI);
		Flash->Deck.Add(C);
	}
	Flash->FinishSpawning(FTransform(Muzzle));
	Live.Add(Flash);
	++Spawned;
	return Flash;
}

FTransform AMuzzleFlash::CardTransform(const FCard& C, const FVector& CamLoc) const
{
	// ONE function, used by the first frame and by every frame after it. It was
	// written twice - once in Tick and once as FTransform::Identity in BeginPlay
	// - and the second copy was simply wrong.
	const FVector World = GetActorLocation() + C.Offset;
	const FVector ToCam = (CamLoc - World).GetSafeNormal();
	const FQuat Face = FRotationMatrix::MakeFromZ(ToCam).ToQuat();
	const FQuat Spun = Face * FQuat(FVector::UpVector, C.Roll);
	const float S = C.Size / 100.f;    // the engine plane is 100 cm square
	return FTransform(Spun, World - GetActorLocation(), FVector(S, S, S));
}

void AMuzzleFlash::BeginPlay()
{
	Super::BeginPlay();

	FlashMaterial = Cards ? Cards->CreateAndSetMaterialInstanceDynamic(0) : nullptr;
	if (FlashMaterial)
	{
		// Age01 BEFORE the first draw. The material's default is whatever the
		// asset was saved with, and a flash whose first frame is drawn at some
		// other age is a flash that flickers on.
		FlashMaterial->SetScalarParameterValue(TEXT("Age01"), 0.f);
	}
	if (Cards)
	{
		// AT THEIR REAL TRANSFORMS, not at identity. AddInstance(Identity) put
		// three cards at the actor's origin, unrotated and at scale 1 - which on
		// the 100 cm engine plane is three coincident one-metre quads lying flat.
		// Tick fixed them on the NEXT frame, so every flash opened with one frame
		// of that; and a flash whose whole life is 0.10 s can be destroyed on its
		// first tick if the frame was long, in which case that wrong frame is the
		// only one it ever had.
		Cards->ClearInstances();
		for (const FCard& C : Deck)
		{
			Cards->AddInstance(CardTransform(C, GetActorLocation()
				+ FVector(0.f, 0.f, 1000.f)), false);
		}
	}
}

void AMuzzleFlash::Tick(float DeltaSeconds)
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
	// rather than being a zero nobody can move. Counted per tick, not latched
	// per flash: for a "never" counter the more sensitive form is the right one.
	if (Age >= LifeSeconds)
	{
		++Stranded;
	}

	const float Age01 = FMath::Clamp(Age / FMath::Max(0.001f, LifeSeconds), 0.f, 1.f);

	// The camera, for billboarding. From the camera manager DIRECTLY: the ocean's
	// focus helper returns the PAWN first, which from abeam faces the cards
	// ninety degrees wrong and shows them edge-on. The smoke paid for that once.
	FVector CamLoc = GetActorLocation() + FVector(0.f, 0.f, 1000.f);
	if (const APlayerController* PC = GetWorld()
		? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->PlayerCameraManager)
		{
			CamLoc = PC->PlayerCameraManager->GetCameraLocation();
		}
	}

	TArray<FTransform> Xforms;
	Xforms.Reserve(Deck.Num());
	for (const FCard& C : Deck)
	{
		// NOTHING MOVES. The cards sit where the gun put them for the whole tenth
		// of a second, and that is the point: a flash that drifts is a small
		// fire, and the ship has already moved on by the time it would show.
		Xforms.Add(CardTransform(C, CamLoc));
	}

	if (Cards && Xforms.Num() == Cards->GetInstanceCount())
	{
		Cards->BatchUpdateInstancesTransforms(0, Xforms, false, true, false);
	}

	if (FlashMaterial)
	{
		FlashMaterial->SetScalarParameterValue(TEXT("Age01"), Age01);
	}
}
