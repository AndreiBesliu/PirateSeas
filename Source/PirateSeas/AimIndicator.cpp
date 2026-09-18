#include "AimIndicator.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "MaterialTypes.h"
#include "Materials/MaterialInstance.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"
#include "ShipPawn.h"
#include "UObject/ConstructorHelpers.h"
#include "WaterBodyComponent.h"

namespace
{
	/** -AimMarks=0 turns the picture off. It exists so the pair of runs that
	 *  proves the marks move no physics can be made at all: one flag, one row,
	 *  and every ballistic number in it must be identical. */
	bool MarksWanted()
	{
		static bool bResolved = false;
		static bool bOn = true;
		if (!bResolved)
		{
			bResolved = true;
			int32 Want = 1;
			if (FParse::Value(FCommandLine::Get(), TEXT("AimMarks="), Want))
			{
				bOn = Want != 0;
			}
		}
		return bOn;
	}
}

AAimIndicator::AAimIndicator()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the hull has moved and after the guns have been laid, or the marks
	// trail the ship by a frame and swim on every turn.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Marks = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Marks"));
	SetRootComponent(Marks);
	Marks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marks->SetGenerateOverlapEvents(false);
	Marks->SetCastShadow(false);
	Marks->SetMobility(EComponentMobility::Movable);
	Marks->bUseAsyncCooking = false;
	Marks->bAffectDistanceFieldLighting = false;
	Marks->bAffectDynamicIndirectLighting = false;
	Marks->SetVisibleInRayTracing(false);

	// ITS OWN material, and the first version reused the trail's. That drew BLACK
	// LINES, for the reason this project keeps re-learning in new clothes: the
	// trail takes its colour from a PARAMETER and its alpha from the vertex, so
	// setting the parameter to white and tinting the vertices left every mark at
	// a luminance of one against a white point of 5793. M_AimMark reads the tint
	// off the vertex and carries the candelas in a scalar, which is the only
	// arrangement that lets three colours share one mesh.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(
		TEXT("/Game/Materials/M_AimMark.M_AimMark"));
	if (Mat.Succeeded())
	{
		Marks->SetMaterial(0, Mat.Object);
	}
}

void AAimIndicator::BeginPlay()
{
	Super::BeginPlay();
	MarkMaterial = Marks ? Marks->CreateAndSetMaterialInstanceDynamic(0) : nullptr;
	float BrightBack = -1.f;
	bool bHasBright = false;
	if (MarkMaterial)
	{
		FParse::Value(FCommandLine::Get(), TEXT("AimBright="), Brightness);
		MarkMaterial->SetScalarParameterValue(TEXT("AimBright"), Brightness);
		MarkMaterial->SetScalarParameterValue(TEXT("AimOpacity"), 1.f);
		// Read back WITH the bool. A readback that cannot fail is not a readback:
		// the trail's first version threw this bool away and printed the value it
		// had just stored, which would have looked identical had the parameter
		// never existed.
		bHasBright = MarkMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("AimBright")), BrightBack);
	}
	UE_LOG(LogTemp, Display,
		TEXT("AIMLOG ready: %s, line %.0f cm wide, arc %.0f m, ride %.0f cm, mat=%s"),
		MarksWanted() ? TEXT("ON") : TEXT("off (-AimMarks=0)"),
		LineHalfCm * 2.f, ArcLengthCm * 0.01f, RideHeightCm,
		MarkMaterial ? TEXT("dynamic") : TEXT("NONE"));
	UE_LOG(LogTemp, Display,
		TEXT("AIMLOG brightness set=%.0f read=%.0f cd/m2 found=%d%s"),
		Brightness, BrightBack, bHasBright ? 1 : 0,
		bHasBright ? TEXT("") : TEXT("  <-- MISSING, the marks will render BLACK"));
}

AAimIndicator* AAimIndicator::For(AShipPawn* InShip)
{
	if (!InShip || !InShip->GetWorld() || !MarksWanted())
	{
		return nullptr;
	}
	UWorld* World = InShip->GetWorld();
	for (TActorIterator<AAimIndicator> It(World); It; ++It)
	{
		It->Ship = InShip;
		return *It;
	}
	AAimIndicator* Made = World->SpawnActor<AAimIndicator>(
		AAimIndicator::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (Made)
	{
		Made->Ship = InShip;
	}
	return Made;
}

float AAimIndicator::SurfaceZAt(const FVector& Where) const
{
	// The SAME query the splash uses, with waves included. The sea is displaced
	// on the GPU in M_Sea, so anything that assumes a flat plane at zero is
	// wrong by up to the wave height - which on this ocean is a metre, and the
	// marks would spend half their life buried.
	if (Ship)
	{
		if (UWaterBodyComponent* Water = Ship->GetWaterBody())
		{
			const EWaterBodyQueryFlags Flags = EWaterBodyQueryFlags::ComputeLocation
				| EWaterBodyQueryFlags::IncludeWaves;
			const TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> Q =
				Water->TryQueryWaterInfoClosestToWorldLocation(Where, Flags);
			if (Q.HasValue() && !Q.GetValue().IsInExclusionVolume())
			{
				return Q.GetValue().GetWaterSurfaceLocation().Z;
			}
		}
	}
	return 0.f;
}

void AAimIndicator::Lay(const FVector& Start, const FVector& Dir, float Length,
	float HalfCm, const FLinearColor& Colour, float Alpha, bool bSquareEnd,
	TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals,
	TArray<FVector2D>& UVs, TArray<FLinearColor>& Colours)
{
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / FMath::Max(100.f, SampleCm)), 2, 64);
	const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal() * HalfCm;
	const int32 Base = Verts.Num();

	for (int32 i = 0; i <= Steps; ++i)
	{
		const float T = (float)i / (float)Steps;
		FVector P = Start + Dir * (Length * T);
		P.Z = SurfaceZAt(P) + RideHeightCm;

		// Faded at the far end unless the caller wants a square finish. An arc
		// line that simply stops reads as a wall across the sea; one that dies
		// away reads as a limit, which is what it is.
		const float A = bSquareEnd ? Alpha : Alpha * (1.f - T * T);
		Verts.Add(P - Side);
		Verts.Add(P + Side);
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(T, 0.f));
		UVs.Add(FVector2D(T, 1.f));
		Colours.Add(FLinearColor(Colour.R, Colour.G, Colour.B, A));
		Colours.Add(FLinearColor(Colour.R, Colour.G, Colour.B, A));
	}
	for (int32 i = 0; i < Steps; ++i)
	{
		const int32 A = Base + i * 2;
		Tris.Add(A);     Tris.Add(A + 1); Tris.Add(A + 2);
		Tris.Add(A + 1); Tris.Add(A + 3); Tris.Add(A + 2);
	}
	++Segments;
}

void AAimIndicator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Marks)
	{
		return;
	}
	// Nothing to draw until the player has actually taken the guns in hand. A
	// picture that appeared before he had laid anything would be answering a
	// question he had not asked, and would sit over the sea in every scenario
	// that has no player at the keyboard at all.
	if (!Ship || !Ship->IsLayingByHand() || Ship->IsOutOfTheFight())
	{
		if (Segments != 0)
		{
			Marks->ClearMeshSection(0);
			Segments = 0;
		}
		return;
	}

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colours;
	Segments = 0;

	const float SideSign = Ship->IsLayingStarboard() ? 1.f : -1.f;
	const FVector Beam = (Ship->GetActorRightVector() * SideSign).GetSafeNormal2D();
	// FROM THE GUN PORTS, not from the middle of the ship. Started at the actor
	// origin the lines came out THROUGH THE DECK, which reads as a bug even when
	// the geometry is right - the marks are what the guns can see, and the guns
	// stand at the ship's side.
	const FVector Origin = Ship->GetActorLocation() + Beam * 700.f;
	const float Limit = Ship->GetMaxTraverseDeg();

	// THE ARC: the two carriage stops. Faint, because they are the standing
	// truth about the ship and not news - what is news is where the guns point
	// and whether they have run out of room.
	for (int32 s = -1; s <= 1; s += 2)
	{
		const FVector D = Beam.RotateAngleAxis(Limit * s, FVector::UpVector);
		Lay(Origin, D, ArcLengthCm, LineHalfCm * 0.6f, ArcTint, 0.30f, false,
			Verts, Tris, Normals, UVs, Colours);
	}

	// THE LAY: where the barrels actually point, out to where the ball falls.
	// It turns amber the moment the mouse asks for more than the carriages have,
	// and that colour is the entire lesson: the line has stopped moving and the
	// only thing left that can move it is the helm.
	const FVector LayDir = Beam.RotateAngleAxis(Ship->GetLayTrainDeg(), FVector::UpVector);
	const float FallCm = FMath::Clamp(
		Ship->RangeForElevationCm(Ship->GetLayElevationDeg()), 500.f, 120000.f);
	const bool bStopped = Ship->IsAgainstTheStop();
	Lay(Origin, LayDir, FallCm, LineHalfCm, bStopped ? StopTint : LayTint,
		bStopped ? 0.85f : 0.62f, true, Verts, Tris, Normals, UVs, Colours);

	// THE FALL: a bar across the lay at the range this elevation reaches. This
	// is the dial the mouse wheel turns, and without it the wheel is a knob with
	// no reading on it.
	const FVector FallAt = Origin + LayDir * FallCm;
	const FVector Across = FVector::CrossProduct(LayDir, FVector::UpVector).GetSafeNormal();
	const float BarHalf = FMath::Max(600.f, FallCm * 0.035f);
	Lay(FallAt - Across * BarHalf, Across, BarHalf * 2.f, LineHalfCm * 1.4f,
		bStopped ? StopTint : LayTint, 0.9f, true,
		Verts, Tris, Normals, UVs, Colours);

	if (Verts.Num() == 0)
	{
		Marks->ClearMeshSection(0);
		return;
	}
	TArray<FProcMeshTangent> Tangents;
	Marks->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colours,
		Tangents, /*bCreateCollision*/ false);

	LogTimer += DeltaSeconds;
	if (LogTimer >= 2.f)
	{
		LogTimer = 0.f;
		UE_LOG(LogTemp, Display,
			TEXT("AIMLOG side=%s train=%+.1f elev=%.1f fall=%.0fm stop=%d locked=%d segs=%d"),
			Ship->IsLayingStarboard() ? TEXT("starboard") : TEXT("port"),
			Ship->GetLayTrainDeg(), Ship->GetLayElevationDeg(), FallCm * 0.01f,
			bStopped ? 1 : 0, Ship->IsLayLocked() ? 1 : 0, Segments);
	}
}
