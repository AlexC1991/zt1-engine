# Entity Attribute & AI Registry

This document archives the numerical constants used to drive the simulation logic.

## 1. Core Simulation Variables
Based on the extracted `.ai` scripts, the 2025 engine must implement these logic loops:

| Attribute | Logic Type | Description |
| :--- | :--- | :--- |
| `fHungerRate` | Float | Rate at which the hunger meter increases per tick. |
| `fThirstRate` | Float | Rate at which the thirst meter increases per tick. |
| `nHappiness` | Integer | Base satisfaction level of the entity. |
| `nHabitat` | Integer | ID matching the preferred terrain documented in `TERRAIN_ARCHITECTURE.md`. |

## 2. Sample Attribute Dumps
