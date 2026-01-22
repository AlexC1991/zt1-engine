# Object Behavior & AI Logic Documentation

This document archives the logic required to simulate ZT1 entities in the 2025 engine.

## 1. Entity Property Mapping
Every created object in the world contains a property block. Based on reverse engineering, these blocks follow this standard:
- **nNameID:** Links to `lang.dll` for the display name.
- **nIconID:** Links to the UI sprite for the purchase menu.
- **nPurchaseCost:** The financial value subtracted from the zoo budget.

## 2. Animation State Machine Architecture
Entities change visual states based on the following triggers which must be documented in the remake code:
| State | Trigger | Animation File (.ani) |
| :--- | :--- | :--- |
| `Idle` | No active task | `[entity]/idle` |
| `Walk` | Pathfinding active | `[entity]/walk` |
| `Eat` | Hunger variable > threshold | `[entity]/eat` |

## 3. World Interaction Rules
The 'Creator' logic for environment satisfaction is determined by:
- **Terrain Compatibility:** A mapping of Animal ID to Terrain ID (e.g., Lion -> Savannah Grass).
- **Object Interactivity:** Defines which objects allow 'rubbing', 'climbing', or 'hiding' behaviors.
