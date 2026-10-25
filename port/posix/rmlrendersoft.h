/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The UI renderer for the POSIX build, in place of UIRmlBgfxRenderClass. It rasterises
** RmlUi's triangles into the UI layer (softscreen.h), which the presenter lays over the game
** frame. It supports what the bgfx renderer supports: textures, vertex colours,
** premultiplied-alpha blending, scissoring, transforms and clip masks.
**
** RmlUi renders every document in full each frame, but a menu that is only standing there
** sends the same commands every time. The commands are kept, and a frame whose commands match
** the last frame's leaves the layer as it is. Where only a few differ (a button the pointer is
** over), only the box they cover, before and after, is cleared and drawn again; otherwise the
** whole layer is. Geometry and textures don't change once made, so a handle stands for its
** contents, as long as it hasn't been released in the meantime.
*/
#pragma once

#include "ui/rml/rmlrender.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

class UIRmlSoftRenderClass : public UIRmlRenderClass
{
	public:
		virtual bool Init(void) override;
		virtual void Shutdown(void) override;
		virtual void Begin_Frame(int x, int y, int width, int height) override;
		virtual void Begin_Dev_Frame(int x, int y, int width, int height) override;
		virtual void Render_ImGui(ImDrawData * data) override;
		virtual void Destroy_ImGui_Textures(void) override;
		virtual int Texture_Limit(void) const override;
		virtual void Log_Resource_Counts(char const * when) const override;
		virtual void Set_Art_Magnification(int factor) override { ArtMagnification = factor < 1 ? 1 : factor; }

		virtual Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
		virtual void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
		virtual void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
		virtual Rml::TextureHandle LoadTexture(Rml::Vector2i & dimensions, Rml::String const & source) override;
		virtual Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
		virtual void ReleaseTexture(Rml::TextureHandle texture) override;
		virtual void EnableScissorRegion(bool enable) override;
		virtual void SetScissorRegion(Rml::Rectanglei region) override;
		virtual void SetTransform(Rml::Matrix4f const * transform) override;
		virtual void EnableClipMask(bool enable) override;
		virtual void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

	protected:
		virtual void Report(char const * message) override;

	public:
		struct Texture
		{
			int Width = 0;
			int Height = 0;
			std::vector<std::uint32_t> Pixels; // premultiplied, 0xAABBGGRR as RmlUi lays bytes out
		};

		// A vertex after the transform, in screen pixels, with its colour premultiplied.
		struct ScreenVertex
		{
			float X, Y;
			float U, V;
			std::uint8_t R, G, B, A;
		};

		enum DrawKind
		{
			DRAW_COLOUR,		// blend into the screen
			DRAW_MASK_WRITE,	// set the stencil to a value
			DRAW_MASK_INCREMENT	// raise the stencil by one
		};

		// Ends the frame begun by Begin_Frame: the layer then holds everything it drew.
		void Finish_Frame(void);

	private:
		// One drawing command, with the state it was drawn in.
		struct Command
		{
			bool Mask = false;	// RenderToClipMask rather than RenderGeometry
			Rml::ClipMaskOperation Operation = Rml::ClipMaskOperation::Set;
			Rml::CompiledGeometryHandle Geometry = 0;
			Rml::Vector2f Translation;
			Rml::TextureHandle Texture = 0;
			bool ScissorEnabled = false;
			Rml::Rectanglei Scissor = Rml::Rectanglei::MakeInvalid();
			bool TransformEnabled = false;
			float Transform[16] = {};
			bool ClipMaskEnabled = false;
			bool Dead = false;	// its geometry or texture has been released: it matches nothing
			int Box[4] = {};	// the pixels it can touch: x0, y0, x1, y1 (x1 and y1 excluded)

			bool operator==(Command const & other) const;
		};

		bool Clip_Rect(int & x0, int & y0, int & x1, int & y1) const;
		void Draw_Triangles(std::vector<ScreenVertex> const & vertices, std::vector<int> const & indices, Texture const * texture, DrawKind kind, std::uint8_t value);
		void Transform_Vertices(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, std::vector<ScreenVertex> & out) const;
		void Ensure_Stencil(void);
		Command Current_Command(void) const;
		void Submit(Command const & command);
		void Execute(Command & command);
		void Find_Box(Command & command, std::vector<ScreenVertex> const & vertices) const;
		void Redraw_Commands_So_Far(void);
		bool Redraw_Part(void);
		void Release_Handle(Rml::CompiledGeometryHandle geometry, Rml::TextureHandle texture);

		std::vector<Command> Commands;		// this frame's so far
		std::vector<Command> LastCommands;	// the frame the layer shows
		bool Drawing = false;				// this frame differs, so its commands are being drawn
		bool LayerStale = true;				// the layer can't be kept, whatever the commands
		bool Differs = false;				// some of this frame's commands differ, drawn at its end
		bool Restricted = false;			// drawing only inside Restrict (x0, y0, x1, y1)
		int Restrict[4] = {};
		bool InFrame = false;
		int LastView[4] = {};

		bool IsReady = false;
		bool NoFastQuads = getenv("OPENTS_NOFASTQUADS") != nullptr; // for comparing the two paths
		int ViewX = 0;
		int ViewY = 0;
		int ViewWidth = 0;
		int ViewHeight = 0;
		bool ScissorEnabled = false;
		Rml::Rectanglei Scissor = Rml::Rectanglei::MakeInvalid();
		int ArtMagnification = 1;
		bool TransformEnabled = false;
		float Transform[16] = {};
		bool ClipMaskEnabled = false;
		std::uint8_t StencilReference = 0;
		std::vector<std::uint8_t> Stencil;
		int StencilWidth = 0;
		int StencilHeight = 0;
		Texture White;
};
