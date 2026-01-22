#ifndef ENUMS_HPP
#define ENUMS_HPP
#include <cstdint>

constexpr int MAX_SPECIES = 20;

enum class EntityType { Animal, Guest, Staff, Object, None };
enum class AnimalSpecies { Lion, Tiger, Grizzly, PolarBear, Gorilla, Chimp, Leopard, Cheetah, Hippo, Zebra, Giraffe, Moose, Elephant, Ostrich, Flamingo, Crocodile, Penguin, None };
enum class AnimState { Idle, Walking, Eating, Drinking, Sleeping, Playing, Run, Swim };
enum class ObjectType { Rock, Tree, Fence, Building, None };

#endif
