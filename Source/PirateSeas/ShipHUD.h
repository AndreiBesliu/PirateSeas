#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ShipHUD.generated.h"

class AShipPawn;
class UWindSubsystem;
class UFont;

/**
 * The ship's instruments, drawn on the canvas in C++.
 *
 * No widget blueprints and no UI textures: this project is built by script and
 * Unreal's Python API cannot author blueprint nodes, so the panel is lines,
 * rectangles and text. That turns out to suit it. A square rig needs one
 * instrument above all others, and it is not a compass rose pointing north:
 * it is a picture of where the power is. The centrepiece is therefore a polar
 * drawn bow-up, showing for every heading she could turn to how hard the rig
 * would pull there, with the dead sector shaded and the best angle for
 * beating marked. A player can then see that bearing away ten degrees gains
 * ground even though it feels slower, which is the one thing the simulation
 * knows and the player could not possibly guess.
 */
UCLASS()
class PIRATESEAS_API AShipHUD : public AHUD
{
	GENERATED_BODY()

public:
	AShipHUD();

	virtual void DrawHUD() override;
	/** The panel's numbers are logged from HERE, not from DrawHUD. DrawHUD is
	 *  gated on FApp::CanEverRender(), which -NullRHI turns off, so a log
	 *  written inside it says nothing at all in exactly the runs that exist to
	 *  measure things. It said nothing for a day and looked like a feature. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Bearing off the wind that makes the most ground to windward. NOT
	 *  derived from the drive curve: the curve alone says 70 degrees, and a
	 *  full polar sweep measured the best at 60 because leeway grows with
	 *  drive. The measurement wins. */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float BestBeatAngleDeg = 60.f;

	/** Radius of the polar, as a fraction of the viewport height, so the
	 *  panel is the same size on any screen. */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float RoseRadiusFraction = 0.145f;

	/** Up in the corner, against open sky. Bow-up it wants to be on the
	 *  centreline, but the centreline is where the ship is: both at the top
	 *  and at the bottom it sat squarely over the thing it describes. */
	UPROPERTY(EditAnywhere, Category = "HUD")
	FVector2D RoseCentreFraction = FVector2D(0.845f, 0.245f);

	/** Below this fraction of a bar, it turns to the warning colour. */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float WarnFraction = 0.34f;

private:
	AShipPawn* GetShip() const;

	/** The polar: where the power is, drawn bow-up. */
	void DrawRose(AShipPawn* Ship, UWindSubsystem* Wind);
	/** Hull, masts, rudder: what is left of her. */
	void DrawCondition(AShipPawn* Ship);
	/** Both batteries, their reload, and which way the guns are pointed. */
	void DrawGuns(AShipPawn* Ship);
	/** Speed, leeway and the ground she is actually making to windward. */
	void DrawWay(AShipPawn* Ship);
	/** In irons, sinking, dismasted: the things worth interrupting for. */
	void DrawWarnings(AShipPawn* Ship, UWindSubsystem* Wind);

	/** One labelled bar. Returns the Y below it, so callers can stack. */
	float DrawBar(float X, float Y, float Width, const FString& Label,
		float Fraction, const FString& Value);

	/** A point on the rose at a bearing relative to the bow, bow-up. */
	FVector2D RosePoint(float RelativeDeg, float Radius) const;

	/** A filled disc, drawn as scanlines: there is no circle primitive, and a
	 *  square panel behind a round instrument looks like a mistake. */
	void DrawDisc(const FVector2D& Centre, float Radius, const FLinearColor& Colour) const;

	FVector2D RoseCentre = FVector2D::ZeroVector;
	float RoseRadius = 100.f;
	/** One text height, the unit everything is spaced by. */
	float Line = 14.f;
	float Scale = 1.f;

	UFont* Big = nullptr;
	UFont* Small = nullptr;

	/** So the numbers behind the pixels can be checked from a headless run
	 *  that never draws anything. */
	float LogTimer = 0.f;
};
