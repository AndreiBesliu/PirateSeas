#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AimIndicator.generated.h"

class AShipPawn;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Where the guns are pointed and where the ball will fall.
 *
 * Asked for in the same breath as the aiming itself: "tot ar trebui ceva care sa
 * arate directia in care vor merge ghiulelele". With the guns laid by hand and
 * the elevation on the mouse wheel, this stops being decoration and becomes the
 * dial on the knob - winding elevation with nothing to read is not aiming, it is
 * guessing.
 *
 * THREE MARKS, and each one answers a different question:
 *   - the ARC, two faint lines on the water at the carriage stops, which say how
 *     much room the guns have before the ship has to be turned;
 *   - the LAY, one bright line where the barrels actually point, which the
 *     player drives with the mouse and which STOPS at the arc while the mouse
 *     keeps going - that refusal is how the whole rule is learned, without a
 *     word of text anywhere;
 *   - the FALL, a bar across the lay line at the range the current elevation
 *     drops a ball at, which is what the wheel is winding.
 *
 * DRAWN ON THE WATER, not on the screen. A 2D overlay could say the same things
 * and would be cheaper, but it would say them in a space the player is not
 * looking at: the whole act is looking along the barrels at a ship, and a mark
 * that lives out there with her can be compared with her by eye, which is the
 * entire skill being asked for.
 *
 * ON A PROCEDURAL MESH, like AShotTrail, and for the reason recorded there at
 * length: an InstancedStaticMeshComponent in this project renders one material,
 * black, and no other.
 *
 * AND ITS HEIGHT IS SAMPLED, not assumed. The sea is displaced in M_Sea on the
 * GPU, so a mark drawn flat at z = 0 would spend half its life inside a wave.
 * Every vertex is lifted to the water surface under it and then a little above.
 */
UCLASS()
class PIRATESEAS_API AAimIndicator : public AActor
{
	GENERATED_BODY()

public:
	AAimIndicator();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** Finds or makes the one indicator in the world and points it at this ship.
	 *  Called by the ship herself, so nothing has to be placed in the level -
	 *  this project builds its levels from script and an actor that has to be
	 *  dragged in is an actor that will be missing from somebody's map. */
	static AAimIndicator* For(AShipPawn* Ship);

	/** Segments drawn on the last rebuild, for the log line. */
	int32 GetSegments() const { return Segments; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Aim")
	TObjectPtr<UProceduralMeshComponent> Marks;

	/** How wide the lines are on the water. Two metres reads as a line at two
	 *  hundred metres and does not become a road at twenty. */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float LineHalfCm = 100.f;

	/** How far the arc lines run. Not to the horizon: a line that never ends
	 *  reads as a wall and hides the sea. Far enough to bracket any range these
	 *  guns can reach. */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float ArcLengthCm = 45000.f;

	/** How high above the water the marks float. Enough to clear the chop, low
	 *  enough that the parallax against a ship at range stays honest. */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float RideHeightCm = 40.f;

	/** Metres of line per sample. The marks follow the swell, so a long line
	 *  needs enough vertices to sit on it rather than cut through it. */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float SampleCm = 1500.f;

	/** How bright the marks burn, in candelas per square metre. It lives HERE and
	 *  not in the tints because a vertex colour is eight bits and clamps at one,
	 *  so it physically cannot carry this number - and this scene's white point
	 *  is at least 5793, so a tint of 1.0 with no scalar behind it is black. See
	 *  ShotTrail.h for the whole account of what that cost. */
	UPROPERTY(EditAnywhere, Category = "Aim")
	float Brightness = 5200.f;

	/** The tints, 0..1. The candelas come from Brightness above. */
	UPROPERTY(EditAnywhere, Category = "Aim")
	FLinearColor ArcTint = FLinearColor(0.42f, 0.58f, 0.92f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Aim")
	FLinearColor LayTint = FLinearColor(1.0f, 0.96f, 0.84f, 1.f);

	/** What the lay line turns when the guns are hard against the stop. Amber,
	 *  because that is what this project's HUD already uses for a warning, and a
	 *  second vocabulary of colours would have to be learned separately. */
	UPROPERTY(EditAnywhere, Category = "Aim")
	FLinearColor StopTint = FLinearColor(1.0f, 0.44f, 0.06f, 1.f);

private:
	UPROPERTY(Transient)
	TObjectPtr<AShipPawn> Ship;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkMaterial;

	int32 Segments = 0;
	float LogTimer = 0.f;

	/** Lays one line on the water from Start along Dir for Length, of the given
	 *  half width, fading to nothing at the far end unless bSquare. Appends to
	 *  the buffers so the whole picture is one mesh section and one draw. */
	void Lay(const FVector& Start, const FVector& Dir, float Length, float HalfCm,
		const FLinearColor& Colour, float Alpha, bool bSquareEnd,
		TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals,
		TArray<FVector2D>& UVs, TArray<FLinearColor>& Colours);

	float SurfaceZAt(const FVector& Where) const;
};
