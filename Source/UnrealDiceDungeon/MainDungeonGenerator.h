#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MainDungeonGenerator.generated.h"

class UArrowComponent;

UCLASS()
class UNREALDICEDUNGEON_API AMainDungeonGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMainDungeonGenerator();

protected:
	virtual void BeginPlay() override;

	// Configurable variables for dungeon generation
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings")
	int32 NumRoomsToGenerate = 10;

	UPROPERTY(EditAnywhere, Category = "Dungeon Settings")
	int32 RandomSeed = 0; // If set to 0, a random seed will be generated

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon Generation")
	TSubclassOf<AActor> FirstRoomBlueprint;

	UPROPERTY(EditAnywhere, Category = "Dungeon Generation")
	TArray<TSubclassOf<AActor>> RoomBlueprints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon Generation")
	TSubclassOf<AActor> ShopBlueprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon Generation")
	TSubclassOf<AActor> EndRoomBlueprint;

	UPROPERTY(EditAnywhere, Category = "Dungeon Generation")
	TArray<TSubclassOf<AActor>> CorridorBlueprints;

	// Internal dungeon generation functions
	void GenerateDungeon();
	bool GenerateNextRoom();

	// Utility functions
	void AlignActorToArrow(UArrowComponent* TargetArrow, UArrowComponent* SourceArrow, AActor* ActorToMove);
	TArray<UArrowComponent*> GetExitArrows(AActor* RoomActor, const FName& ExitListName = "ExitList");
	bool CheckOverlap(AActor* Actor);
	void DestroyArrowWithChildren(USceneComponent* Arrow);

	// Runtime data
	TSet<UArrowComponent*> UsedArrows;
	TArray<AActor*> SpawnedRooms;
	int32 RoomsSpawned = 0;

	// Random number generator
	FRandomStream DungeonRNG; // Central RNG for dungeon generation
};