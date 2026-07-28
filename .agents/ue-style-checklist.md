# Unreal Style Checklist

Source: https://github.com/Allar/ue5-style-guide/tree/v2

Use this checklist for any Unreal content, Blueprint, C++, or folder changes in this project.

## Non-negotiables

- Keep existing project conventions if they already conflict with the guide.
- Make the project look like it was authored by one person.
- Avoid illegal or unlicensed content.
- Never use spaces, unicode, or symbol characters in identifiers.
- Prefer only `[A-Za-z0-9_]+` for names.

## Asset naming

- Use the correct asset prefix for every asset.
- Keep names clear, descriptive, and searchable.
- Use PascalCase for the base asset name.
- Use the guide’s prefixes for common types:
  - `BP_` Blueprint
  - `WBP_` Widget Blueprint
  - `BPI_` Blueprint Interface
  - `BPFL_` Blueprint Function Library
  - `M_` Material
  - `MI_` Material Instance
  - `T_` Texture
  - `SK_` Skeletal Mesh
  - `SM_` Static Mesh
  - `A_` Animation Sequence / Sound Wave, where applicable
  - `ABP_` Animation Blueprint
  - `BT_`, `BB_`, `AIC_` for AI assets
- Use `b` prefix for booleans.
- Use PascalCase for non-boolean Blueprint variables.
- Keep enum names prefixed with `E`.
- Keep structs prefixed with `F` or `S`.

## Folder structure

- Put all project content under one top-level project folder.
- Use PascalCase for folders.
- Do not use spaces in folder names.
- Do not use unicode or special symbols in folder names.
- Keep maps in a `Maps` folder.
- Keep critical base gameplay classes in `Core`.
- Use `MaterialLibrary` for shared materials, functions, and reusable utility textures.
- Do not create redundant folders like `Assets`, `Meshes`, `Textures`, or `Materials` just to sort by type.
- Allow exception folders only for very large related asset sets, such as shared animation or audio libraries.
- Do not leave empty folders in the Content Browser.

## Blueprints

- All Blueprints must compile with zero warnings and zero errors.
- Fix Blueprint warnings and errors immediately.
- Do not submit broken Blueprints.
- Put base gameplay classes in `Core`, and use child classes elsewhere for project-specific behavior.

## Practical review rule

Before committing, check:

- names
- prefixes
- folder placement
- map location
- Core vs child class placement
- Blueprint compile state
- empty folders

If something looks inconsistent, fix it before it spreads.
