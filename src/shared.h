#include "base/all.h"

#ifndef GAMESHARED_H
#define GAMESHARED_H

///// #define some game-tunable constants
#define GAME_CONSTANT_ONE (1)
#define GAME_CONSTANT_TWO (2)

typedef enum EntityFeature {
  FeatureWalksAround,
  FeatureCanFight,
  EntityFeature_Count
} EntityFeature;

typedef enum EntityType {
  EntityNull,
  EntityWall,
  EntityDoor,
  EntityCharacter,
  EntityType_Count,
} EntityType;
static const char* ENTITY_STRINGS[] = {
  "NULL",
  "Wall",
  "Door",
  "Character",
};

typedef struct {
  i32 x;
  i32 y;
  i32 z;
} XYZ;

typedef enum Direction {
  DirectionInvalid,
  North,
  South,
  East,
  West,
  Up,
  Down,
  Direction_Count
} Direction;

typedef enum CommandType {
  CommandInvalid,
  CommandKeepAlive,
  CommandLogin,
  CommandCreateCharacter,
  CommandType_Count,
} CommandType;
static const char* command_type_strings[] = {
  "Invalid",
  "KeepAlive",
  "Login",
  "CreateCharacter",
};

#define ENTITY_HEADER_MESSAGE_SIZE (8+8+1+1+1)
#define ENTITY_MESSAGE_SIZE (ENTITY_HEADER_MESSAGE_SIZE+2+2+2+2+8+1+1)
typedef enum Message {
  MessageInvalid,
  MessageCharacterId,
  MessageBadPw,
  MessageNewAccountCreated,
  Message_Count,
} Message;
static const char* MESSAGE_STRINGS[] = {
  "Invalid",
  "CharacterId",
  "BadPw",
  "NewAccountCreated",
};

#endif //GAMESHARED_H
