#include "LightingAssets.hpp"
namespace LightingAssets {
// addition: surveyed native water/steam regions. These receive environment
// light; they have no point light sources and never produce autonomous light.
void appendWaterDefinitions(std::vector<Definition> &definitions) {
  {
    Definition a{400,"dfnt1"};a.animated=true;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-49.88f,-8.74f},{-15.08f,-7.26f},{-6.96f,-23.54f},{12.76f,-22.06f},{11.6f,6.06f},{-8.12f,11.98f},{-13.92f,1.62f},{-42.92f,0.88f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[0].push_back({"surveyed spray",Material::Water,{{-18.56f,-33.16f},{-11.6f,-33.16f},{2.32f,11.24f},{-8.12f,11.24f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-12.76f,-24.96f},{3.48f,-22.68f},{23.2f,-15.08f},{39.44f,-14.32f},{44.08f,-7.48f},{25.52f,2.4f},{10.44f,12.28f},{1.16f,6.96f},{0.0f,-4.44f},{-13.92f,-9.0f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed spray",Material::Water,{{2.32f,-39.4f},{9.28f,-39.4f},{20.88f,-7.48f},{10.44f,-5.2f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-48.72f,-6.77f},{-17.4f,-3.85f},{-11.6f,-15.53f},{-9.28f,-21.37f},{12.76f,-22.1f},{9.28f,12.21f},{-3.48f,16.59f},{-10.44f,11.48f},{-10.44f,1.26f},{-38.28f,3.45f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed spray",Material::Water,{{-17.4f,-32.32f},{-10.44f,-32.32f},{2.32f,13.67f},{-6.96f,13.67f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-8.12f,-17.72f},{13.92f,-16.99f},{27.84f,-12.61f},{45.24f,-15.53f},{47.56f,-8.23f},{35.96f,-1.66f},{24.36f,7.83f},{2.32f,13.67f},{0.0f,-3.85f},{-9.28f,-7.5f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed spray",Material::Water,{{0.0f,-34.51f},{8.12f,-34.51f},{22.04f,-3.85f},{11.6f,-1.66f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{401,"dfnt2"};a.animated=true;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{13.32f,-33.92f},{41.04f,-38.88f},{71.84f,-25.24f},{57.98f,3.28f},{28.72f,19.4f},{-6.7f,23.12f},{-45.2f,28.08f},{-65.22f,15.68f},{-63.68f,-1.68f},{-37.5f,-1.68f},{-9.78f,14.44f},{21.02f,10.72f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[0].push_back({"surveyed spray",Material::Water,{{8.7f,-86.0f},{39.5f,-86.0f},{51.82f,-38.88f},{24.1f,-33.92f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{8.78f,-32.3f},{38.61f,-25.4f},{66.87f,-10.45f},{65.3f,12.55f},{29.19f,26.35f},{-28.9f,26.35f},{-60.3f,9.1f},{-68.15f,-9.3f},{-47.74f,-9.3f},{-44.6f,3.35f},{-8.49f,11.4f},{15.06f,-1.25f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed spray",Material::Water,{{8.78f,-64.5f},{22.91f,-64.5f},{29.19f,-16.2f},{11.92f,-12.75f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-36.42f,-24.25f},{-5.62f,-21.95f},{15.94f,-0.1f},{45.2f,-3.55f},{60.6f,-10.45f},{66.76f,3.35f},{42.12f,18.3f},{-17.94f,30.95f},{-61.06f,14.85f},{-70.3f,-0.1f},{-56.44f,-18.5f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed spray",Material::Water,{{-48.74f,-63.35f},{-36.42f,-63.35f},{-21.02f,-12.75f},{-36.42f,-9.3f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-65.3f,-30.75f},{-37.04f,-38.25f},{-8.78f,-33.25f},{-5.64f,-7.0f},{22.62f,10.5f},{50.88f,6.75f},{65.01f,4.25f},{55.59f,19.25f},{0.64f,28.0f},{-57.45f,11.75f},{-73.15f,-9.5f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed spray",Material::Water,{{-30.76f,-87.0f},{-18.2f,-87.0f},{-26.05f,-24.5f},{-49.6f,-20.75f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{402,"duckpond"};a.animated=false;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-84.24f,-19.28f},{-38.88f,-37.35f},{2.16f,-42.91f},{49.68f,-31.79f},{71.28f,-10.94f},{62.64f,12.69f},{12.96f,32.15f},{-34.56f,30.76f},{-77.76f,9.91f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-73.44f,-15.2f},{-21.6f,-41.55f},{32.4f,-32.25f},{71.28f,-13.65f},{75.6f,8.05f},{8.64f,35.95f},{-38.88f,20.45f},{-69.12f,3.4f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-64.8f,-17.76f},{-17.28f,-36.32f},{17.28f,-37.48f},{45.36f,-17.76f},{58.32f,8.92f},{23.76f,28.64f},{-19.44f,32.12f},{-64.8f,11.24f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-56.16f,-21.1f},{-15.12f,-37.06f},{19.44f,-34.78f},{60.48f,-13.12f},{66.96f,9.68f},{25.92f,33.62f},{-15.12f,30.2f},{-49.68f,11.96f}},0,{130,185,238,255},true,false,PixelMask::Water});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{403,"fount2"};a.animated=true;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-74.76f,-20.37f},{-48.06f,-42.35f},{-21.36f,-37.64f},{-19.58f,-4.67f},{-37.38f,9.46f},{24.92f,17.31f},{35.6f,6.32f},{28.48f,-31.36f},{51.62f,-37.64f},{78.32f,-10.95f},{72.98f,12.6f},{40.94f,29.87f},{-35.6f,34.58f},{-72.98f,14.17f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[0].push_back({"surveyed spray",Material::Water,{{-14.24f,-113.0f},{7.12f,-113.0f},{12.46f,-65.9f},{-14.24f,-67.47f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-74.76f,-19.14f},{-48.06f,-40.7f},{-21.36f,-36.08f},{-19.58f,-3.74f},{-37.38f,10.12f},{24.92f,17.82f},{35.6f,7.04f},{28.48f,-29.92f},{51.62f,-36.08f},{78.32f,-9.9f},{72.98f,13.2f},{40.94f,30.14f},{-35.6f,34.76f},{-72.98f,14.74f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed spray",Material::Water,{{-14.24f,-110.0f},{7.12f,-110.0f},{12.46f,-63.8f},{-14.24f,-65.34f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-74.76f,-19.14f},{-48.06f,-40.7f},{-21.36f,-36.08f},{-19.58f,-3.74f},{-37.38f,10.12f},{24.92f,17.82f},{35.6f,7.04f},{28.48f,-29.92f},{51.62f,-36.08f},{78.32f,-9.9f},{72.98f,13.2f},{40.94f,30.14f},{-35.6f,34.76f},{-72.98f,14.74f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed spray",Material::Water,{{-14.24f,-110.0f},{7.12f,-110.0f},{12.46f,-63.8f},{-14.24f,-65.34f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-74.76f,-19.96f},{-48.06f,-41.8f},{-21.36f,-37.12f},{-19.58f,-4.36f},{-37.38f,9.68f},{24.92f,17.48f},{35.6f,6.56f},{28.48f,-30.88f},{51.62f,-37.12f},{78.32f,-10.6f},{72.98f,12.8f},{40.94f,29.96f},{-35.6f,34.64f},{-72.98f,14.36f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed spray",Material::Water,{{-14.24f,-112.0f},{7.12f,-112.0f},{12.46f,-65.2f},{-14.24f,-66.76f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{404,"fount3"};a.animated=true;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-56.28f,-16.02f},{-37.52f,-28.56f},{-30.82f,-9.18f},{-38.86f,3.36f},{-26.8f,13.62f},{16.08f,19.32f},{33.5f,12.48f},{34.84f,-5.76f},{37.52f,-27.42f},{57.62f,-16.02f},{61.64f,5.64f},{45.56f,20.46f},{9.38f,28.44f},{-38.86f,23.88f},{-58.96f,5.64f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[0].push_back({"surveyed spray",Material::Water,{{-40.2f,-70.74f},{-24.12f,-73.02f},{-14.74f,-4.62f},{-30.82f,-2.34f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[0].push_back({"surveyed spray",Material::Water,{{-6.7f,-81.0f},{4.02f,-81.0f},{5.36f,-57.06f},{-4.02f,-57.06f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[0].push_back({"surveyed spray",Material::Water,{{16.08f,-69.6f},{32.16f,-67.32f},{33.5f,-4.62f},{17.42f,-2.34f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-56.28f,-12.58f},{-37.52f,-24.24f},{-30.82f,-6.22f},{-38.86f,5.44f},{-26.8f,14.98f},{16.08f,20.28f},{33.5f,13.92f},{34.84f,-3.04f},{37.52f,-23.18f},{57.62f,-12.58f},{61.64f,7.56f},{45.56f,21.34f},{9.38f,28.76f},{-38.86f,24.52f},{-58.96f,7.56f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed spray",Material::Water,{{-40.2f,-63.46f},{-24.12f,-65.58f},{-14.74f,-1.98f},{-30.82f,0.14f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed spray",Material::Water,{{-6.7f,-73.0f},{4.02f,-73.0f},{5.36f,-50.74f},{-4.02f,-50.74f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed spray",Material::Water,{{16.08f,-62.4f},{32.16f,-60.28f},{33.5f,-1.98f},{17.42f,0.14f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-56.28f,-12.58f},{-37.52f,-24.24f},{-30.82f,-6.22f},{-38.86f,5.44f},{-26.8f,14.98f},{16.08f,20.28f},{33.5f,13.92f},{34.84f,-3.04f},{37.52f,-23.18f},{57.62f,-12.58f},{61.64f,7.56f},{45.56f,21.34f},{9.38f,28.76f},{-38.86f,24.52f},{-58.96f,7.56f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed spray",Material::Water,{{-40.2f,-63.46f},{-24.12f,-65.58f},{-14.74f,-1.98f},{-30.82f,0.14f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed spray",Material::Water,{{-6.7f,-73.0f},{4.02f,-73.0f},{5.36f,-50.74f},{-4.02f,-50.74f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed spray",Material::Water,{{16.08f,-62.4f},{32.16f,-60.28f},{33.5f,-1.98f},{17.42f,0.14f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-56.28f,-12.15f},{-37.52f,-23.7f},{-30.82f,-5.85f},{-38.86f,5.7f},{-26.8f,15.15f},{16.08f,20.4f},{33.5f,14.1f},{34.84f,-2.7f},{37.52f,-22.65f},{57.62f,-12.15f},{61.64f,7.8f},{45.56f,21.45f},{9.38f,28.8f},{-38.86f,24.6f},{-58.96f,7.8f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed spray",Material::Water,{{-40.2f,-62.55f},{-24.12f,-64.65f},{-14.74f,-1.65f},{-30.82f,0.45f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed spray",Material::Water,{{-6.7f,-72.0f},{4.02f,-72.0f},{5.36f,-49.95f},{-4.02f,-49.95f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed spray",Material::Water,{{16.08f,-61.5f},{32.16f,-59.4f},{33.5f,-1.65f},{17.42f,0.45f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{405,"geyser1"};a.animated=true;
    a.views[0].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-32},{-60,-32}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-32},{-60,-32}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-32},{-60,-32}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-32},{-60,-32}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{406,"geyser2"};a.animated=true;
    a.views[0].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-18},{-60,-18}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-18},{-60,-18}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-18},{-60,-18}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"native geyser plume",Material::Water,{{-60,-400},{60,-400},{60,-18},{-60,-18}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{407,"jgarden"};a.animated=false;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-84.0f,-7.85f},{-8.4f,-13.55f},{46.2f,6.4f},{75.6f,34.9f},{16.8f,60.55f},{-46.2f,46.3f},{-79.8f,20.65f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{4.0f,19.24f},{80.5f,11.2f},{118.75f,24.6f},{59.25f,67.48f},{-30.0f,51.4f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-121.8f,9.84f},{-50.4f,12.58f},{-33.6f,39.98f},{-75.6f,53.68f},{-138.6f,37.24f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-63.5f,-1.8f},{8.75f,-7.96f},{38.5f,13.6f},{-16.75f,47.48f},{-93.25f,25.92f}},0,{130,185,238,255},true,false,PixelMask::Water});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{408,"tarpit"};a.animated=false;
    a.views[0].push_back({"surveyed tar surface",Material::Water,{{-42.84f,-23.9f},{4.44f,-28.18f},{49.75f,-12.13f},{53.69f,15.69f},{10.35f,34.95f},{-34.96f,25.32f},{-62.54f,8.2f}},0,{130,185,238,255},true,false,PixelMask::Tar});
    a.views[1].push_back({"surveyed tar surface",Material::Water,{{-46.6f,-24.5f},{12.85f,-27.65f},{62.05f,-8.75f},{43.6f,18.55f},{2.6f,32.2f},{-48.65f,19.6f},{-67.1f,0.7f}},0,{130,185,238,255},true,false,PixelMask::Tar});
    a.views[2].push_back({"surveyed tar surface",Material::Water,{{-47.04f,-20.62f},{3.92f,-29.18f},{54.88f,-10.99f},{54.88f,15.76f},{5.88f,30.74f},{-43.12f,22.18f},{-60.76f,-0.29f}},0,{130,185,238,255},true,false,PixelMask::Tar});
    a.views[3].push_back({"surveyed tar surface",Material::Water,{{-51.8f,-20.64f},{3.55f,-26.88f},{56.85f,-15.44f},{63.0f,6.4f},{11.75f,30.32f},{-41.55f,18.88f},{-64.1f,-1.92f}},0,{130,185,238,255},true,false,PixelMask::Tar});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{409,"jappond"};a.animated=false;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-97.81f,-6.04f},{-54.41f,-14.93f},{-41.39f,-36.52f},{-0.16f,-40.33f},{15.03f,-18.74f},{56.26f,-7.31f},{54.09f,18.09f},{30.22f,28.25f},{23.71f,44.76f},{-26.2f,47.3f},{-34.88f,19.36f},{-80.45f,18.09f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-81.6f,-5.5f},{-29.68f,-5.5f},{-27.32f,-27.35f},{8.08f,-42.3f},{31.68f,-27.35f},{31.68f,-17.0f},{83.6f,-7.8f},{90.68f,10.6f},{57.64f,15.2f},{38.76f,23.25f},{19.88f,38.2f},{-15.52f,35.9f},{-8.44f,17.5f},{-67.44f,11.75f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-77.96f,-10.68f},{-41.07f,-31.76f},{6.67f,-36.72f},{28.37f,-20.6f},{32.71f,-6.96f},{80.45f,0.48f},{78.28f,21.56f},{32.71f,21.56f},{26.2f,43.88f},{-17.2f,46.36f},{-25.88f,29.0f},{-56.26f,17.84f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-82.08f,-0.16f},{-39.42f,-3.4f},{-10.98f,-20.68f},{17.46f,-23.92f},{50.64f,-18.52f},{50.64f,-3.4f},{83.82f,7.4f},{79.08f,22.52f},{38.79f,21.44f},{31.68f,44.12f},{-8.61f,46.28f},{-22.83f,27.92f},{-58.38f,19.28f}},0,{130,185,238,255},true,false,PixelMask::Water});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{410,"jmpfount"};a.animated=true;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-28.8f,-4.8f},{7.68f,-25.2f},{29.76f,-21.0f},{-7.68f,0.6f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[0].push_back({"surveyed spray",Material::Water,{{-32.64f,-10.2f},{-12.48f,-10.2f},{-12.48f,-0.6f},{-33.6f,-0.6f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-30.72f,-25.28f},{-17.28f,-29.12f},{32.64f,-2.24f},{16.32f,2.88f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed spray",Material::Water,{{-30.72f,-40.0f},{-15.36f,-40.0f},{-10.56f,-18.24f},{-24.96f,-18.24f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-31.8f,-4.16f},{16.65f,-26.56f},{29.0f,-24.0f},{-19.45f,2.24f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed spray",Material::Water,{{4.3f,-38.08f},{23.3f,-38.08f},{26.15f,-21.44f},{12.85f,-20.16f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-29.95f,-22.8f},{-14.75f,-24.0f},{33.7f,1.8f},{14.7f,3.6f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed spray",Material::Water,{{14.7f,-15.6f},{35.6f,-15.6f},{31.8f,4.2f},{13.75f,0.6f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{411,"junpond"};a.animated=false;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-38.24f,18.52f},{-5.69f,7.54f},{-5.69f,-16.86f},{29.03f,-32.72f},{57.24f,-32.72f},{61.58f,-10.76f},{46.39f,13.64f},{24.69f,41.7f},{-10.03f,41.7f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-90.75f,3.08f},{-63.75f,-15.96f},{-36.75f,-27.16f},{-23.25f,-19.32f},{-41.25f,0.84f},{-3.0f,6.44f},{26.25f,5.32f},{48.75f,4.2f},{78.0f,9.8f},{64.5f,21.0f},{28.5f,26.6f},{-25.5f,17.64f},{-68.25f,13.16f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-74.6f,0.96f},{-37.71f,-16.44f},{-20.35f,-31.52f},{5.69f,-38.48f},{20.88f,-30.36f},{12.2f,-7.16f},{-13.84f,6.76f},{-11.67f,34.6f},{-50.73f,35.76f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-64.5f,-14.5f},{-19.5f,-22.0f},{23.25f,-20.75f},{41.25f,-9.5f},{84.0f,-7.0f},{90.75f,9.25f},{68.25f,18.0f},{43.5f,15.5f},{23.25f,6.75f},{-28.5f,5.5f},{-64.5f,-0.75f}},0,{130,185,238,255},true,false,PixelMask::Water});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{412,"sandwell"};a.animated=false;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-13.2f,4.44f},{2.01f,0.72f},{6.69f,4.44f},{-2.67f,12.5f},{-13.2f,11.26f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-6.12f,4.44f},{4.32f,3.2f},{12.44f,6.3f},{7.8f,10.64f},{-3.8f,10.64f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-13.71f,7.06f},{-2.01f,3.36f},{9.69f,3.36f},{14.37f,6.32f},{2.67f,11.5f},{-9.03f,10.76f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-15.92f,3.34f},{2.64f,2.1f},{10.76f,7.06f},{0.32f,10.78f},{-11.28f,10.78f}},0,{130,185,238,255},true,false,PixelMask::Water});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{413,"vent1"};a.animated=true;
    a.views[0].push_back({"native steam",Material::Steam,{{-60,-400},{60,-400},{60,-24},{-60,-24}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"native steam",Material::Steam,{{-60,-400},{60,-400},{60,-24},{-60,-24}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"native steam",Material::Steam,{{-60,-400},{60,-400},{60,-24},{-60,-24}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"native steam",Material::Steam,{{-60,-400},{60,-400},{60,-24},{-60,-24}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{414,"wishw2"};a.animated=true;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-12.88f,-7.2f},{-6.44f,-10.84f},{-3.22f,-9.28f},{-3.22f,-3.04f},{3.68f,-2.52f},{5.52f,-10.84f},{13.8f,-7.2f},{13.34f,0.08f},{3.22f,3.72f},{-6.9f,1.64f},{-12.88f,-1.48f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[0].push_back({"surveyed spray",Material::Water,{{-4.6f,-41.0f},{4.14f,-41.0f},{5.06f,-25.92f},{-4.6f,-25.92f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-12.88f,-6.85f},{-6.44f,-10.42f},{-3.22f,-8.89f},{-3.22f,-2.77f},{3.68f,-2.26f},{5.52f,-10.42f},{13.8f,-6.85f},{13.34f,0.29f},{3.22f,3.86f},{-6.9f,1.82f},{-12.88f,-1.24f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed spray",Material::Water,{{-4.6f,-40.0f},{4.14f,-40.0f},{5.06f,-25.21f},{-4.6f,-25.21f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-12.88f,-7.2f},{-6.44f,-10.84f},{-3.22f,-9.28f},{-3.22f,-3.04f},{3.68f,-2.52f},{5.52f,-10.84f},{13.8f,-7.2f},{13.34f,0.08f},{3.22f,3.72f},{-6.9f,1.64f},{-12.88f,-1.48f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed spray",Material::Water,{{-4.6f,-41.0f},{4.14f,-41.0f},{5.06f,-25.92f},{-4.6f,-25.92f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-12.88f,-7.2f},{-6.44f,-10.84f},{-3.22f,-9.28f},{-3.22f,-3.04f},{3.68f,-2.52f},{5.52f,-10.84f},{13.8f,-7.2f},{13.34f,0.08f},{3.22f,3.72f},{-6.9f,1.64f},{-12.88f,-1.48f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed spray",Material::Water,{{-4.6f,-41.0f},{4.14f,-41.0f},{5.06f,-25.92f},{-4.6f,-25.92f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
  {
    Definition a{415,"wishwell"};a.animated=true;
    a.views[0].push_back({"surveyed water surface",Material::Water,{{-35.17f,-6.38f},{-22.99f,-10.25f},{-9.07f,-7.67f},{10.94f,-7.67f},{25.73f,-12.83f},{36.17f,-5.09f},{33.56f,2.65f},{12.68f,10.39f},{-12.55f,10.39f},{-32.56f,2.65f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[0].push_back({"surveyed spray",Material::Water,{{-10.81f,-107.0f},{10.94f,-107.0f},{10.07f,-17.99f},{-9.07f,-17.99f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[1].push_back({"surveyed water surface",Material::Water,{{-36.17f,-6.82f},{-23.99f,-10.75f},{-10.07f,-8.13f},{9.94f,-8.13f},{24.73f,-13.37f},{35.17f,-5.51f},{32.56f,2.35f},{11.68f,10.21f},{-13.55f,10.21f},{-33.56f,2.35f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[1].push_back({"surveyed spray",Material::Water,{{-11.81f,-109.0f},{9.94f,-109.0f},{9.07f,-18.61f},{-10.07f,-18.61f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[2].push_back({"surveyed water surface",Material::Water,{{-36.17f,-7.04f},{-23.99f,-11.0f},{-10.07f,-8.36f},{9.94f,-8.36f},{24.73f,-13.64f},{35.17f,-5.72f},{32.56f,2.2f},{11.68f,10.12f},{-13.55f,10.12f},{-33.56f,2.2f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[2].push_back({"surveyed spray",Material::Water,{{-11.81f,-110.0f},{9.94f,-110.0f},{9.07f,-18.92f},{-10.07f,-18.92f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    a.views[3].push_back({"surveyed water surface",Material::Water,{{-35.17f,-6.38f},{-22.99f,-10.25f},{-9.07f,-7.67f},{10.94f,-7.67f},{25.73f,-12.83f},{36.17f,-5.09f},{33.56f,2.65f},{12.68f,10.39f},{-12.55f,10.39f},{-32.56f,2.65f}},0,{130,185,238,255},true,false,PixelMask::Water});
    a.views[3].push_back({"surveyed spray",Material::Water,{{-10.81f,-107.0f},{10.94f,-107.0f},{10.07f,-17.99f},{-9.07f,-17.99f}},0,{130,185,238,255},true,true,PixelMask::Spray});
    definitions.push_back(std::move(a));
  }
}
}
