#pragma once

// ---------------------------------------------------------------------------------------------------------------------
// The layout of our own prefabs. The client's files hold no data for the real prefabs (no level, no records, no positions), only the pieces each
// one is made of (class and how many), so the layout is worked out here: from what the pieces measure and how their top surface is shaped.
//
// This file knows nothing about the game (no engine types): it takes a list of piece types and returns where each piece goes, so the result can be
// drawn and looked at on its own (tools/PrefabPreview). All lengths are in game units, X east, Y north, Z up, yaw in degrees; (0, 0) is the middle
// of the prefab's footprint and Z = 0 the ground.
//
// What a prefab becomes:
//  - a playset with walls, roofs or floors becomes a building: a core of slots (the pitch of its wall panels) with a yard round it, levels as the
//    prefab is tall, the door in front, windows high up, arches spread out, a gable or shed roof with its end caps, a hay loft or floor, stairs,
//    corner posts, and the props tidily in the yard and inside;
//  - a playset that only has props (a gallery, a gameplay prop, a target) is shown as an exhibit: one row per kind of piece.
// ---------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef ARCHITECT_TRACE
#define ARCHITECT_STAGE(Name) (std::fprintf(stderr, "  stage %s\n", Name), std::fflush(stderr))
#else
#define ARCHITECT_STAGE(Name) ((void)0)
#endif

namespace Architect
{
	enum class Kind { Floor, Roof, RoofCap, Stairs, Door, Window, Opening, Wall, Rail, Pillar, Trim, Feature, Prop };

	inline const char* KindName(Kind K)
	{
		static const char* Names[] = { "floor", "roof", "roof cap", "stairs", "door", "window", "opening", "wall", "rail", "pillar", "trim", "feature", "prop" };
		return Names[(int)K];
	}

	// the same without a space, the form the log and tools/PrefabPreview use
	inline const char* KindToken(Kind K)
	{
		static const char* Names[] = { "floor", "roof", "roofcap", "stairs", "door", "window", "opening", "wall", "rail", "pillar", "trim", "feature", "prop" };
		return Names[(int)K];
	}

	// The shape of the top surface of a piece, from a grid of rays dropped on it: flat, one side high (a shed roof, stairs, a gable cap that is a
	// right triangle) or high in the middle (a tent: a gable roof tile, a cap that is an isosceles triangle).
	enum class Shape { Box, Shed, Tent };

	struct Type
	{
		std::string Name;
		Kind K = Kind::Prop;
		int Count = 0;
		double SX = 100, SY = 100, SZ = 100; // the box at yaw 0
		Shape Sh = Shape::Box;
		int HighAxis = 0; // Shed: the axis the piece rises along (0 X, 1 Y)
		int HighSign = 0; // Shed: +1 when the high side is at the larger coordinate
		int RidgeAxis = 0; // Tent: the axis its ridge runs along
		bool Measured = false;

		double Long() const { return (std::max)(SX, SY); }
		double Short() const { return (std::min)(SX, SY); }
		bool LongAlongX() const { return SX >= SY; }
		int ThinAxis() const { return SX < SY ? 0 : 1; }
	};

	struct Placement
	{
		int Type = -1;
		double X = 0, Y = 0; // the middle of the piece's box
		double Z = 0; // the underside of the piece's box
		double Yaw = 0;
		int AlongX = -1; // 1 when the piece's long side must run along X, 0 along Y, -1 when it does not matter
		double ScaleX = 1, ScaleY = 1, ScaleZ = 1; // the actor's scale along its own axes (roofs are stretched to fit their span)
	};

	struct Request
	{
		int SizeX = 1, SizeY = 1, SizeZ = 1; // the prefab's size in tiles
		std::string Name;
		std::vector<Type> Types;
		double Tile = 512.0; // one tile of the prefab grid
		double LevelUnit = 384.0; // one unit of the prefab's height
	};

	struct Result
	{
		std::vector<Placement> Items;
		std::vector<std::string> Notes;
		std::vector<Type> Types; // as the planner understood them (kinds corrected from the measurements)
		bool bExhibit = false;
		int CoreX = 0, CoreY = 0, Levels = 0;
		double Pitch = 512, Step = 384;
		double MinX = 0, MaxX = 0, MinY = 0, MaxY = 0; // the core of the building
	};

	inline std::string Fmt(const char* Format, double A = 0, double B = 0, double C = 0, double D = 0)
	{
		char Buffer[320];
		std::snprintf(Buffer, sizeof(Buffer), Format, A, B, C, D);
		return Buffer;
	}

	inline std::string Lower(std::string Text)
	{
		for (auto& Char : Text)
			Char = (char)std::tolower((unsigned char)Char);

		return Text;
	}

	inline bool Has(const std::string& Text, const char* Word) { return Lower(Text).find(Word) != std::string::npos; }

	// A piece's kind from its class name (the measurements refine it afterwards).
	inline Kind KindFromName(const std::string& Name)
	{
		const auto Text = Lower(Name);
		auto Contains = [&](const char* Word) { return Text.find(Word) != std::string::npos; };

		if (Contains("stair") || Contains("ramp"))
			return Kind::Stairs;

		if (Contains("steeple") || Contains("cupola") || Contains("chimney") || Contains("weathervane") || Contains("antenna") || Contains("spire"))
			return Kind::Feature;

		if (Contains("roof") || Contains("ceiling"))
			return Contains("cap") ? Kind::RoofCap : Kind::Roof;

		if (Contains("floor") || Contains("ground") || Contains("slab") || Contains("deck") || Contains("foundation"))
			return Kind::Floor;

		if (Contains("window"))
			return Kind::Window;

		if (Contains("doorway") || Contains("arch") || Contains("opening") || Contains("gate"))
			return Kind::Opening;

		if (Contains("door"))
			return Kind::Door;

		if (Contains("pillar") || Contains("column") || Contains("post") || Contains("pole"))
			return Kind::Pillar;

		if (Contains("halfwall") || Contains("half_wall") || Contains("quarterwall") || Contains("quarter_wall") || Contains("railing") || Contains("rail") || Contains("fence") || Contains("parapet"))
			return Kind::Rail;

		if (Contains("wall_light") || Contains("wallart") || Contains("wall_art") || Contains("painting") || Contains("poster") || Contains("banner") || Contains("sign"))
			return Kind::Prop;

		if (Contains("battlement") || Contains("trim") || Contains("balcony") || Contains("beam") || Contains("gutter") || Contains("cap"))
			return Kind::Trim;

		if (Contains("wall"))
			return Kind::Wall;

		return Kind::Prop;
	}

	inline void RotateXY(double YawDegrees, double X, double Y, double& OutX, double& OutY)
	{
		const double Radians = YawDegrees * 3.14159265358979 / 180.0;
		const double C = std::cos(Radians), S = std::sin(Radians);
		OutX = X * C - Y * S;
		OutY = X * S + Y * C;
	}

	// The yaw (a quarter turn) that points a shed piece's high side the wanted way and, when asked, puts its thin axis along ThinAxis.
	inline double PickYaw(const Type& T, int WantAxis, int WantSign, int ThinAxis)
	{
		double Best = 0;
		int BestScore = -1;

		for (int Turn = 0; Turn < 4; ++Turn)
		{
			const double Yaw = Turn * 90.0;
			int Score = 0;

			if (WantAxis >= 0 && T.Sh == Shape::Shed)
			{
				double HX = T.HighAxis == 0 ? T.HighSign : 0, HY = T.HighAxis == 1 ? T.HighSign : 0, RX, RY;
				RotateXY(Yaw, HX, HY, RX, RY);
				const int Axis = std::fabs(RX) > 0.5 ? 0 : 1;
				const int Sign = (Axis == 0 ? RX : RY) > 0 ? 1 : -1;

				if (Axis == WantAxis && Sign == WantSign)
					Score += 2;
			}

			if (ThinAxis >= 0)
			{
				const int Thin = (Turn % 2 == 0) ? T.ThinAxis() : 1 - T.ThinAxis();

				if (Thin == ThinAxis)
					Score += 4;
			}

			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Yaw;
			}
		}

		return Best;
	}

	class Builder
	{
	public:
		explicit Builder(const Request& Req) : R(Req), T(Req.Types)
		{
			// a prefab is a few hundred pieces; a list that is far bigger is scaled down so one throw can not flood the server
			int All = 0;

			for (auto& Type : T)
				All += (std::max)(0, Type.Count);

			if (All > 700)
				for (auto& Type : T)
					if (Type.Count > 0)
						Type.Count = (std::max)(1, (int)std::lround(Type.Count * 700.0 / All));

			Left.resize(T.size());

			for (size_t i = 0; i < T.size(); ++i)
				Left[i] = (std::max)(0, T[i].Count);
		}

		Result Run();

	private:
		struct Slot { double X, Y; int Face; int Index; bool bAlongX; };

		Request R;
		std::vector<Type> T;
		std::vector<int> Left;
		Result Out;

		double P = 512, Ls = 384;
		int A = 0, B = 0, L = 1; // the core: A x B slots, L levels
		double MinX = 0, MinY = 0, MaxX = 0, MaxY = 0; // the core
		double VolMinX = 0, VolMinY = 0, VolMaxX = 0, VolMaxY = 0; // the volume
		int Ring = 0;
		std::vector<Slot> Slots;
		std::vector<std::vector<int>> Cell; // [level][slot]: -2 free, -1 left empty, else the type index
		std::set<int> StairCells; // interior cells (x + y * A) taken by stairs
		double RoofTop = 0; // the highest point of the roof
		int PartitionPieces = 0; // inside walls and arcades placed
		int RidgeAxis = 0;

		// ---- helpers
		std::vector<int> Of(Kind K) const
		{
			std::vector<int> Result;

			for (size_t i = 0; i < T.size(); ++i)
				if (T[i].K == K && T[i].Count > 0)
					Result.push_back((int)i);

			std::stable_sort(Result.begin(), Result.end(), [&](int X, int Y) { return T[X].Count > T[Y].Count; });
			return Result;
		}

		int Total(Kind K) const
		{
			int N = 0;

			for (auto& Type : T)
				if (Type.K == K)
					N += Type.Count;

			return N;
		}

		int LeftOf(Kind K) const
		{
			int N = 0;

			for (size_t i = 0; i < T.size(); ++i)
				if (T[i].K == K)
					N += Left[i];

			return N;
		}

		bool Take(int Index)
		{
			if (Index < 0 || Left[Index] <= 0)
				return false;

			--Left[Index];
			return true;
		}

		void Put(int Index, double X, double Y, double Z, double Yaw, int AlongX = -1, double ScaleX = 1, double ScaleY = 1, double ScaleZ = 1)
		{
			Placement Item;
			Item.Type = Index;
			Item.X = X; Item.Y = Y; Item.Z = (std::max)(0.0, Z); Item.Yaw = Yaw; Item.AlongX = AlongX; // nothing goes under the ground
			Item.ScaleX = ScaleX; Item.ScaleY = ScaleY; Item.ScaleZ = ScaleZ;
			Out.Items.push_back(Item);
		}

		void Note(const std::string& Text) { Out.Notes.push_back(Text); }

		static double Footprint(const Type& Type, double Yaw, bool bX)
		{
			const bool bTurned = std::fmod(std::fabs(Yaw), 180.0) > 45.0;
			return bX ? (bTurned ? Type.SY : Type.SX) : (bTurned ? Type.SX : Type.SY);
		}

		int RingDistance(int I, int J) const
		{
			const int D = std::abs(I - J);
			return (std::min)(D, Ring - D);
		}

		// Picks Count slots out of Candidates, each as far as possible from the slots already taken (Avoid) and from each other.
		std::vector<int> PickSpread(const std::vector<int>& Candidates, int Count, std::vector<int> Avoid) const
		{
			std::vector<int> Picked;
			std::vector<int> Pool = Candidates;

			while ((int)Picked.size() < Count && !Pool.empty())
			{
				int Best = -1, BestDistance = -1;

				for (size_t i = 0; i < Pool.size(); ++i)
				{
					int Nearest = Ring;

					for (int Other : Avoid)
						Nearest = (std::min)(Nearest, RingDistance(Pool[i], Other));

					if (Nearest > BestDistance)
					{
						BestDistance = Nearest;
						Best = (int)i;
					}
				}

				if (BestDistance < 2 && !Avoid.empty() && Picked.size() >= 1)
					break; // nothing left that is not next to another

				Picked.push_back(Pool[Best]);
				Avoid.push_back(Pool[Best]);
				Pool.erase(Pool.begin() + Best);
			}

			return Picked;
		}

		double PanelYaw(const Type& Type, int Face) const
		{
			const bool bFaceAlongX = (Face == 0 || Face == 2);
			double Yaw = (Type.LongAlongX() == bFaceAlongX) ? 0.0 : 90.0;

			if (Face == 2 || Face == 3)
				Yaw += 180.0;

			return Yaw;
		}

		// ---- stages
		void RefineKinds();
		bool Exhibit();
		void ChooseGrid();
		void BuildRing();
		void Facade();
		void Partitions();
		void Pillars();
		void Floors();
		void StairsStage();
		void Rails();
		void Roof();
		void Features();
		void Awning();
		void Trims();
		void Props();

	};

	// ---------------------------------------------------------------------------------------------------------------------
	inline void Builder::RefineKinds()
	{
		// the height of a full wall panel
		double RefHeight = 0;

		for (auto& Type : T)
			if (Type.K == Kind::Wall && Type.Long() > 350 && Type.Long() < 700)
				RefHeight = (std::max)(RefHeight, Type.SZ);

		if (RefHeight <= 0)
			RefHeight = 384;

		for (auto& Type : T)
		{
			if ((Type.K == Kind::Wall || Type.K == Kind::Window || Type.K == Kind::Opening) && Type.SZ < 0.55 * RefHeight)
				Type.K = Kind::Rail; // a quarter wall or a railing

			if (Type.K == Kind::Wall && Type.Long() < 0.6 * 512.0)
				Type.K = Kind::Pillar; // a narrow filler piece becomes a corner post
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// An exhibit: one row for each kind of piece, left to right, front to back, centred.
	inline bool Builder::Exhibit()
	{
		Out.bExhibit = true;

		std::vector<int> Order;

		for (size_t i = 0; i < T.size(); ++i)
			if (T[i].Count > 0)
				Order.push_back((int)i);

		// the same kinds together, big pieces first
		std::stable_sort(Order.begin(), Order.end(), [&](int X, int Y) {
			if (T[X].K != T[Y].K)
				return (int)T[X].K < (int)T[Y].K;

			return T[X].SX * T[X].SY > T[Y].SX * T[Y].SY;
		});

		// the rows: every class on its own rows, the width of the exhibit grows with the amount of pieces
		int TotalPieces = 0;

		for (int Index : Order)
			TotalPieces += T[Index].Count;

		double Area = 0;

		for (int Index : Order)
			Area += T[Index].Count * (T[Index].SX + 80) * (T[Index].SY + 80);

		const double Width = (std::max)(R.SizeX * R.Tile, (std::min)(std::sqrt(Area) * 1.3, 9000.0));

		// lay the classes out one after another, each starting on a new row
		double Y = 0;
		struct Row { std::vector<std::pair<int, double>> Items; double Depth = 0; double Width = 0; };
		std::vector<Row> Rows;

		for (int Index : Order)
		{
			Row Current;
			int N = Left[Index];

			for (int n = 0; n < N; ++n)
			{
				const double W = T[Index].SX;

				if (!Current.Items.empty() && Current.Width + W + 80 > Width)
				{
					Rows.push_back(Current);
					Current = Row();
				}

				Current.Items.push_back({ Index, W });
				Current.Width += W + (Current.Items.size() > 1 ? 80 : 0);
				Current.Depth = (std::max)(Current.Depth, T[Index].SY);
			}

			Left[Index] = 0;

			if (!Current.Items.empty())
				Rows.push_back(Current);
		}

		double TotalDepth = 0;

		for (auto& Row : Rows)
			TotalDepth += Row.Depth + 100;

		Y = -TotalDepth / 2;

		for (auto& Row : Rows)
		{
			double X = -Row.Width / 2;

			for (auto& Item : Row.Items)
			{
				Put(Item.first, X + Item.second / 2, Y + Row.Depth / 2, 0, 0);
				X += Item.second + 80;
			}

			Y += Row.Depth + 100;
		}

		Out.Pitch = 512;
		Note(Fmt("exhibit: %.0f pieces in %.0f rows", (double)TotalPieces, (double)Rows.size()));
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	inline void Builder::ChooseGrid()
	{
		// the pitch: what the wall panels measure along their edge
		{
			std::vector<std::pair<double, int>> Widths;

			for (auto& Type : T)
				if ((Type.K == Kind::Wall || Type.K == Kind::Window || Type.K == Kind::Opening) && Type.Long() > 350 && Type.Long() < 700)
					Widths.push_back({ Type.Long(), Type.Count });

			std::sort(Widths.begin(), Widths.end());
			int All = 0;

			for (auto& W : Widths)
				All += W.second;

			P = 512;

			if (All > 0)
			{
				int Seen = 0;

				for (auto& W : Widths)
				{
					Seen += W.second;

					if (Seen * 2 >= All)
					{
						P = std::fabs(W.first - 512.0) <= 64 ? 512.0 : W.first;
						break;
					}
				}
			}
		}

		// the height of a level: the shortest of the main wall panels (the thicker ones then overlap a little instead of leaving a gap)
		{
			int MaxCount = 0;

			for (auto& Type : T)
				if (Type.K == Kind::Wall || Type.K == Kind::Window || Type.K == Kind::Opening)
					MaxCount = (std::max)(MaxCount, Type.Count);

			double Shortest = 0;

			for (auto& Type : T)
				if ((Type.K == Kind::Wall || Type.K == Kind::Window || Type.K == Kind::Opening) && Type.Count >= (std::max)(2, MaxCount / 6) && Type.Long() > 350)
					Shortest = Shortest == 0 ? Type.SZ : (std::min)(Shortest, Type.SZ);

			Ls = Shortest > 100 ? Shortest : 384.0;
		}

		double RoofHeight = 0;

		for (auto& Type : T)
			if (Type.K == Kind::Roof)
				RoofHeight = (std::max)(RoofHeight, Type.SZ);

		const int VX = (std::max)(1, (int)std::lround(R.SizeX * R.Tile / P));
		const int VY = (std::max)(1, (int)std::lround(R.SizeY * R.Tile / P));
		VolMinX = -VX * P / 2; VolMaxX = VX * P / 2;
		VolMinY = -VY * P / 2; VolMaxY = VY * P / 2;

		// the width of the door (in slots) and how many doors: the front is made so that the door sits in the middle of it
		int DoorUnits = 0;
		double DoorWide = 0;

		for (int Index : Of(Kind::Door))
		{
			DoorUnits = (std::max)(DoorUnits, T[Index].Count);
			DoorWide = (std::max)(DoorWide, T[Index].Long());
		}

		const int DoorSlots = DoorUnits > 0 ? (std::max)(1, (int)std::lround(DoorWide / P)) : 1;

		// the slots the wall panels have to fill: walls, windows and arches, and the width of the doors
		const int PanelSlots = Total(Kind::Wall) + Total(Kind::Window) + Total(Kind::Opening) + DoorUnits * DoorSlots;
		int Props = 0;

		for (auto& Type : T)
			if (Type.K == Kind::Prop)
				Props += Type.Count;

		const int MaxLevels = (std::max)(1, (std::min)((int)std::floor((R.SizeZ * R.LevelUnit - 0.8 * RoofHeight) / Ls + 0.5), 6));

		if (PanelSlots <= 0)
		{
			// no walls: a pavilion on the whole footprint
			A = VX; B = VY; L = 1;
			MinX = VolMinX; MaxX = VolMaxX; MinY = VolMinY; MaxY = VolMaxY;
			return;
		}

		// Every core that fits is scored: how many wall slots stay open or how many panels are left over for the inside, how far the shape is from the
		// volume's, whether the door can be centred, and whether the building leaves a yard when there are props to put in it. Levels: the fewest that
		// leave no more than 40 percent of the panels over (a tower may be as tall as the volume).
		double BestScore = 1e18;
		int BestA = 0, BestB = 0, BestL = 1;

		for (int Margin = 0; Margin <= 1; ++Margin)
		{
			if (Margin == 1 && (VX < 4 || VY < 4))
				continue;

			for (int Ca = (VX > 1 ? 2 : 1); Ca <= VX - 2 * Margin; ++Ca)
				for (int Cb = (VY > 1 ? 2 : 1); Cb <= VY - 2 * Margin; ++Cb)
				{
					const int Perimeter = 2 * (Ca + Cb);
					int Levels = MaxLevels;

					for (int Try = 1; Try <= MaxLevels; ++Try)
						if (PanelSlots <= 1.4 * Perimeter * Try)
						{
							Levels = Try;
							break;
						}

					// buildings with a footprint of a few slots are towers; the rest stays low
					if (VX * VY > 6)
						Levels = (std::min)(Levels, (std::max)(3, (PanelSlots + Perimeter - 1) / Perimeter));

					const int Slots = Perimeter * Levels;
					double Score = Slots >= PanelSlots ? (Slots - PanelSlots) * 3.0 : (PanelSlots - Slots) * 1.0;
					Score += std::fabs(std::log((double)Ca / Cb) - std::log((double)VX / VY)) * 6.0;

					if (DoorUnits > 0 && ((Ca - DoorSlots) % 2 != 0))
						Score += 6;

					if (DoorUnits == 0 && Total(Kind::Opening) > 0 && Ca % 2 == 0)
						Score += 8;

					if (Margin == 0 && VX >= 4 && VY >= 4 && Props > 0)
						Score += 25;

					if (Levels > 3 && VX * VY > 6)
						Score += (Levels - 3) * 20.0;

					if (Score < BestScore)
					{
						BestScore = Score;
						BestA = Ca; BestB = Cb; BestL = Levels;
					}
				}
		}

		if (BestA > 0)
		{
			A = BestA; B = BestB; L = BestL;
		}
		else
		{
			// too many panels for any core: the whole footprint, as many levels as it takes
			A = (std::max)(1, VX); B = (std::max)(1, VY);
			L = (std::min)(8, (PanelSlots + 2 * (A + B) - 1) / (2 * (A + B)));
		}

		MinX = -A * P / 2; MaxX = A * P / 2;
		MinY = -B * P / 2; MaxY = B * P / 2;
	}

	inline void Builder::BuildRing()
	{
		Slots.clear();

		for (int k = 0; k < A; ++k) Slots.push_back({ MinX + (k + 0.5) * P, MinY, 0, k, true });
		for (int k = 0; k < B; ++k) Slots.push_back({ MaxX, MinY + (k + 0.5) * P, 1, k, false });
		for (int k = 0; k < A; ++k) Slots.push_back({ MaxX - (k + 0.5) * P, MaxY, 2, k, true });
		for (int k = 0; k < B; ++k) Slots.push_back({ MinX, MaxY - (k + 0.5) * P, 3, k, false });

		Ring = (int)Slots.size();
		Cell.assign(L, std::vector<int>(Ring, -2));
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// The walls: the door in front, arches spread along the ground floor, windows up high, plain walls everywhere else.
	inline void Builder::Facade()
	{
		auto FaceLength = [&](int Face) { return (Face == 0 || Face == 2) ? A : B; };
		auto SlotIndex = [&](int Face, int Index) {
			int Base = 0;

			for (int f = 0; f < Face; ++f)
				Base += FaceLength(f);

			return Base + Index;
		};

		// -- doors: every door piece type is one part of a door, stacked; a unit goes in the front, then back, then the sides
		auto Doors = Of(Kind::Door);
		int Units = 0;
		double DoorWide = 0;

		for (int Index : Doors)
		{
			Units = (std::max)(Units, Left[Index]);
			DoorWide = (std::max)(DoorWide, T[Index].Long());
		}

		const int DoorSlots = (std::max)(1, (int)std::lround(DoorWide / P));
		std::stable_sort(Doors.begin(), Doors.end(), [&](int X, int Y) {
			const bool BaseX = Has(T[X].Name, "base"), BaseY = Has(T[Y].Name, "base");

			if (BaseX != BaseY)
				return BaseX;

			return T[X].SZ > T[Y].SZ;
		});

		const int FaceOrder[4] = { 0, 2, 1, 3 };
		int DoorsPlaced = 0;

		for (int Unit = 0; Unit < Units && Unit < 4; ++Unit)
		{
			const int Face = FaceOrder[Unit];
			const int Length = FaceLength(Face);
			const int Wide = (std::max)(1, (std::min)(DoorSlots, Length)); // a door is never wider than its face
			const int First = (Length - Wide) / 2;
			double X = 0, Y = 0;

			for (int s = 0; s < Wide; ++s)
			{
				auto& Slot = Slots[SlotIndex(Face, First + s)];
				X += Slot.X; Y += Slot.Y;
				Cell[0][SlotIndex(Face, First + s)] = -1;
			}

			X /= Wide; Y /= Wide;
			double Z = 0;

			for (int Index : Doors)
			{
				if (!Take(Index))
					continue;

				Put(Index, X, Y, Z, PanelYaw(T[Index], Face), (Face == 0 || Face == 2) ? 1 : 0);
				Z += T[Index].SZ;
			}

			// a door taller than one level also takes the slots above it
			if (Z > 1.3 * Ls && L > 1)
				for (int s = 0; s < Wide; ++s)
					Cell[1][SlotIndex(Face, First + s)] = -1;

			++DoorsPlaced;
		}

		for (int Index : Doors)
			if (Left[Index] > 0)
				Note(Fmt("%.0f spare pieces of a door were left over", (double)Left[Index]));

		// -- the entrance when there is no door piece: an arch in the middle of the front, or just a gap
		auto Openings = Of(Kind::Opening);

		if (DoorsPlaced == 0)
		{
			const int Slot = SlotIndex(0, A / 2);
			Cell[0][Slot] = -1;

			if (!Openings.empty() && Take(Openings[0]))
			{
				auto& S = Slots[Slot];
				Put(Openings[0], S.X, S.Y, 0, PanelYaw(T[Openings[0]], 0), 1);
			}
		}

		// -- arches: spread over the ground level (at most 40 percent of it), the rest high up at the front and the back
		{
			std::vector<int> Taken;

			for (int k = 0; k < Ring; ++k)
				if (Cell[0][k] == -1)
					Taken.push_back(k);

			int Wanted = 0;

			for (int Index : Openings)
				Wanted += Left[Index];

			const int GroundCap = (std::max)(0, (int)(0.4 * Ring) - (int)Taken.size());
			std::vector<int> Candidates;

			for (int k = 0; k < Ring; ++k)
			{
				const int Face = Slots[k].Face;
				const int Length = FaceLength(Face);
				const bool bCorner = Slots[k].Index == 0 || Slots[k].Index == Length - 1;

				if (Cell[0][k] == -2 && !bCorner)
					Candidates.push_back(k);
			}

			auto Picked = PickSpread(Candidates, (std::min)(Wanted, GroundCap), Taken);
			size_t Next = 0;

			for (int Slot : Picked)
			{
				while (Next < Openings.size() && Left[Openings[Next]] <= 0)
					++Next;

				if (Next >= Openings.size())
					break;

				Take(Openings[Next]);
				Cell[0][Slot] = Openings[Next];
			}

			// high up: the middle of the front and back on the levels above
			for (int Level = 1; Level < L; ++Level)
			{
				std::vector<int> Upper;

				for (int Face : { 0, 2 })
				{
					const int Index = SlotIndex(Face, FaceLength(Face) / 2);

					if (Cell[Level][Index] == -2)
						Upper.push_back(Index);
				}

				for (int Slot : Upper)
				{
					while (Next < Openings.size() && Left[Openings[Next]] <= 0)
						++Next;

					if (Next >= Openings.size())
						break;

					Take(Openings[Next]);
					Cell[Level][Slot] = Openings[Next];
				}
			}
		}

		// -- windows: on the second level (the first when there is only one), spread round the building
		{
			auto Windows = Of(Kind::Window);
			int Wanted = 0;

			for (int Index : Windows)
				Wanted += Left[Index];

			const int Level = L > 1 ? 1 : 0;
			std::vector<int> Candidates, Taken;

			for (int k = 0; k < Ring; ++k)
			{
				const int Length = FaceLength(Slots[k].Face);
				const bool bCorner = Slots[k].Index == 0 || Slots[k].Index == Length - 1;

				if (Cell[Level][k] == -2 && !bCorner)
					Candidates.push_back(k);

				if (Cell[Level][k] != -2)
					Taken.push_back(k);
			}

			auto Picked = PickSpread(Candidates, (std::min)(Wanted, (int)(0.34 * Ring) + 1), Taken);
			size_t Next = 0;

			for (int Slot : Picked)
			{
				while (Next < Windows.size() && Left[Windows[Next]] <= 0)
					++Next;

				if (Next >= Windows.size())
					break;

				Take(Windows[Next]);
				Cell[Level][Slot] = Windows[Next];
			}

			// more windows than that: the other levels, spread the same way
			for (int Other = 0; Other < L && Wanted > 0; ++Other)
			{
				if (Other == Level)
					continue;

				int Remaining = 0;

				for (int Index : Windows)
					Remaining += Left[Index];

				if (Remaining <= 0)
					break;

				Candidates.clear();
				Taken.clear();

				for (int k = 0; k < Ring; ++k)
				{
					const int Length = FaceLength(Slots[k].Face);
					const bool bCorner = Slots[k].Index == 0 || Slots[k].Index == Length - 1;

					if (Cell[Other][k] == -2 && !bCorner)
						Candidates.push_back(k);

					if (Cell[Other][k] != -2)
						Taken.push_back(k);
				}

				Picked = PickSpread(Candidates, (std::min)(Remaining, (int)(0.34 * Ring) + 1), Taken);
				Next = 0;

				for (int Slot : Picked)
				{
					while (Next < Windows.size() && Left[Windows[Next]] <= 0)
						++Next;

					if (Next >= Windows.size())
						break;

					Take(Windows[Next]);
					Cell[Other][Slot] = Windows[Next];
				}
			}
		}

		// -- the plain walls (and whatever arches and windows are left): a strip of one face on one level gets one kind where it can
		{
			std::vector<int> Pool;

			for (int Index : Of(Kind::Wall)) Pool.push_back(Index);

			// when the plain walls are used up the arches, then the windows, close what is left of the ring (what is still over after that becomes inside walls)
			for (int Index : Of(Kind::Opening)) if (Left[Index] > 0) Pool.push_back(Index);
			for (int Index : Of(Kind::Window)) if (Left[Index] > 0) Pool.push_back(Index);

			auto Best = [&]() -> int {
				// the plain wall type with the most pieces left, else the first of the others that has any
				int Pick = -1;

				for (int Index : Pool)
					if (Left[Index] > 0 && T[Index].K == Kind::Wall && (Pick < 0 || Left[Index] > Left[Pick]))
						Pick = Index;

				if (Pick >= 0)
					return Pick;

				for (int Index : Pool)
					if (Left[Index] > 0)
						return Index;

				return -1;
			};

			for (int Level = 0; Level < L; ++Level)
				for (int Face : FaceOrder)
				{
					int Pick = -1;

					for (int k = 0; k < Ring; ++k)
					{
						if (Slots[k].Face != Face || Cell[Level][k] != -2)
							continue;

						if (Pick < 0 || Left[Pick] <= 0)
							Pick = Best();

						if (Pick < 0)
							break;

						Take(Pick);
						Cell[Level][k] = Pick;
					}
				}
		}

		// -- put the pieces down
		for (int Level = 0; Level < L; ++Level)
			for (int k = 0; k < Ring; ++k)
				if (Cell[Level][k] >= 0)
				{
					const int Index = Cell[Level][k];
					Put(Index, Slots[k].X, Slots[k].Y, Level * Ls, PanelYaw(T[Index], Slots[k].Face), Slots[k].bAlongX ? 1 : 0);
				}

		int Open = 0;

		for (int Level = 0; Level < L; ++Level)
			for (int k = 0; k < Ring; ++k)
				if (Cell[Level][k] == -2)
					++Open;

		if (Open > 0)
			Note(Fmt("%.0f wall slots stay open (not enough wall pieces)", (double)Open));
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Panels left over after the outside is closed (many arches, spare walls): inside walls. The arches come first, as arcades on lines along the long
	// side (they split the inside into an aisle and bays), then the plain walls, then lines across.
	inline void Builder::Partitions()
	{
		std::vector<int> Pool;

		for (int Index : Of(Kind::Opening)) if (Left[Index] > 0) Pool.push_back(Index);
		for (int Index : Of(Kind::Wall)) if (Left[Index] > 0) Pool.push_back(Index);
		for (int Index : Of(Kind::Window)) if (Left[Index] > 0) Pool.push_back(Index);

		if (Pool.empty())
			return;

		const int Axis = A > B ? 0 : 1; // the long side
		const int N = Axis == 0 ? A : B;
		const int M = Axis == 0 ? B : A;

		struct Line { double Fixed; bool bAlongX; int Slots; double Start; double Key; };
		std::vector<Line> Lines;

		// along the long side, the middle ones first
		for (int k = 1; k < M; ++k)
		{
			const double V = (Axis == 0 ? MinY : MinX) + k * P;
			const double Key = std::fabs(V);
			Lines.push_back({ V, Axis == 0, N, Axis == 0 ? MinX : MinY, Key });
		}

		std::stable_sort(Lines.begin(), Lines.end(), [](const Line& X, const Line& Y) { return X.Key < Y.Key; });

		// across: one in the middle first
		std::vector<Line> Cross;

		for (int k = 1; k < N; ++k)
		{
			const double U = (Axis == 0 ? MinX : MinY) + k * P;
			Cross.push_back({ U, Axis != 0, M, Axis == 0 ? MinY : MinX, std::fabs(U) });
		}

		std::stable_sort(Cross.begin(), Cross.end(), [](const Line& X, const Line& Y) { return X.Key < Y.Key; });
		Lines.insert(Lines.end(), Cross.begin(), Cross.end());

		auto Next = [&]() -> int { for (int Index : Pool) if (Left[Index] > 0) return Index; return -1; };
		int Placed = 0;

		for (auto& Line : Lines)
			for (int Level = 0; Level < L; ++Level)
				for (int j = 0; j < Line.Slots; ++j)
				{
					const int Index = Next();

					if (Index < 0)
						goto Done;

					Take(Index);
					const double Along = Line.Start + (j + 0.5) * P;
					const double X = Line.bAlongX ? Along : Line.Fixed;
					const double Y = Line.bAlongX ? Line.Fixed : Along;
					Put(Index, X, Y, Level * Ls, T[Index].LongAlongX() == Line.bAlongX ? 0.0 : 90.0, Line.bAlongX ? 1 : 0);
					++Placed;
				}

	Done:
		PartitionPieces = Placed;

		if (Placed > 0)
			Note(std::string("inside walls and arcades: ") + std::to_string(Placed));
	}

	// ---------------------------------------------------------------------------------------------------------------------
	inline void Builder::Pillars()
	{
		auto Posts = Of(Kind::Pillar);

		if (Posts.empty())
			return;

		const double Corners[4][2] = { { MinX, MinY }, { MaxX, MinY }, { MaxX, MaxY }, { MinX, MaxY } };
		size_t Next = 0;

		for (int Level = 0; Level < L; ++Level)
			for (int c = 0; c < 4; ++c)
			{
				while (Next < Posts.size() && Left[Posts[Next]] <= 0)
					++Next;

				if (Next >= Posts.size())
					return;

				Take(Posts[Next]);
				Put(Posts[Next], Corners[c][0], Corners[c][1], Level * Ls, 0);
			}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// The floors: when the building has levels, the first deck is a hay loft round the walls (the middle stays open, the stairs reach it); a single
	// level gets its ground covered, starting at the door.
	inline void Builder::Floors()
	{
		auto Decks = Of(Kind::Floor);

		if (Decks.empty())
			return;

		// the stairs take their cells first (west wall, from the front)
		int StairsPieces = LeftOf(Kind::Stairs);
		const bool bStairs = StairsPieces > 0 && A >= 2 && B >= 2;

		if (bStairs)
			for (int y = 0; y < (std::min)(B - 1, 2); ++y)
				StairCells.insert(0 + y * A);

		struct Cell2 { int X, Y; double Key; };
		std::vector<Cell2> Cells;

		const int Level = L > 1 ? 1 : 0;

		for (int y = 0; y < B; ++y)
			for (int x = 0; x < A; ++x)
			{
				if (Level == 1 && StairCells.count(x + y * A))
					continue;

				const int Edge = (std::min)((std::min)(x, A - 1 - x), (std::min)(y, B - 1 - y));
				const double Key = Level == 1 ? Edge * 100.0 - y : (double)y * 10.0 + std::fabs(x - (A - 1) / 2.0);
				Cells.push_back({ x, y, Key });
			}

		std::stable_sort(Cells.begin(), Cells.end(), [](const Cell2& X, const Cell2& Y) { return X.Key < Y.Key; });

		int Used = 0;
		size_t Next = 0;

		for (auto& C : Cells)
		{
			while (Next < Decks.size() && Left[Decks[Next]] <= 0)
				++Next;

			if (Next >= Decks.size())
				break;

			// an upper deck sits with its top at the level line, a ground floor on the ground
			const auto& Type = T[Decks[Next]];
			Take(Decks[Next]);
			Put(Decks[Next], MinX + (C.X + 0.5) * P, MinY + (C.Y + 0.5) * P, Level == 1 ? Ls - Type.SZ : 0.0, 0);
			++Used;
		}

		Note(std::string("floors: ") + std::to_string(Used) + (Level == 1 ? " on the first level (a loft)" : " on the ground"));

		// what is left covers the ground, or the next level
		for (int Deck = Level == 1 ? 2 : 1; Deck <= (Level == 1 ? L - 1 : 0) + 1 && LeftOf(Kind::Floor) > 0; ++Deck)
		{
			Next = 0;
			int More = 0;

			for (int y = 0; y < B && LeftOf(Kind::Floor) > 0; ++y)
				for (int x = 0; x < A && LeftOf(Kind::Floor) > 0; ++x)
				{
					while (Next < Decks.size() && Left[Decks[Next]] <= 0)
						++Next;

					if (Next >= Decks.size())
						break;

					const auto& Type = T[Decks[Next]];
					Take(Decks[Next]);
					Put(Decks[Next], MinX + (x + 0.5) * P, MinY + (y + 0.5) * P, Deck * Ls - Type.SZ, 0);
					++More;
				}

			if (More > 0 && Deck < L)
				Note(Fmt("%.0f more floors on level %.0f", (double)More, (double)Deck));

			if (Deck >= L)
				break;
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	inline void Builder::StairsStage()
	{
		auto Flights = Of(Kind::Stairs);

		if (Flights.empty() || A < 2 || B < 2)
			return;

		// a straight run along the west wall, going north, starting near the front
		double Y = MinY + 0.25 * P;
		double Z = 0;
		int Placed = 0;

		for (int Index : Flights)
		{
			const auto& Type = T[Index];

			while (Left[Index] > 0 && Placed < 6)
			{
				double Yaw = PickYaw(Type, 1, 1, -1);
				const double W = Footprint(Type, Yaw, true), D = Footprint(Type, Yaw, false);
				Take(Index);
				Put(Index, MinX + W / 2 + 60, Y + D / 2, Z, Yaw);
				Y += D;
				Z += Type.SZ;
				++Placed;

				if (Z >= Ls * 0.95 || Y + D > MaxY - 0.5 * P)
					break;
			}

			if (Z >= Ls * 0.95)
				break;
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Rails along the edge of the loft facing the open middle, else along the top of the front.
	inline void Builder::Rails()
	{
		auto Guards = Of(Kind::Rail);

		if (Guards.empty() || LeftOf(Kind::Rail) <= 0)
			return;

		size_t Next = 0;
		auto Rail = [&](double X, double Y, bool bAlongX, double Z) {
			while (Next < Guards.size() && Left[Guards[Next]] <= 0)
				++Next;

			if (Next >= Guards.size())
				return;

			const auto& Type = T[Guards[Next]];
			Take(Guards[Next]);
			Put(Guards[Next], X, Y, Z, Type.LongAlongX() == bAlongX ? 0.0 : 90.0, bAlongX ? 1 : 0);
		};

		// the loft: its inner edge is the boundary of the open middle (the cells one in from the walls), where the first deck is
		if (L > 1 && A >= 3 && B >= 3)
		{
			const double Z = Ls;

			for (int x = 1; x < A - 1; ++x)
			{
				Rail(MinX + (x + 0.5) * P, MinY + P, true, Z);
				Rail(MinX + (x + 0.5) * P, MaxY - P, true, Z);
			}

			for (int y = 1; y < B - 1; ++y)
			{
				Rail(MinX + P, MinY + (y + 0.5) * P, false, Z);
				Rail(MaxX - P, MinY + (y + 0.5) * P, false, Z);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// The roof. Tents (gable tiles) are laid on every slot with the ridge along the long side; sheds (one slope) are paired along the ridge, as
	// many in a stack as the span needs; caps close the ends; flat pieces are laid like floors.
	inline void Builder::Roof()
	{
		auto Roofs = Of(Kind::Roof);

		if (Roofs.empty())
		{
			RoofTop = L * Ls;
			return;
		}

		const double BaseZ = L * Ls;
		RidgeAxis = A > B ? 0 : 1;
		const int N = RidgeAxis == 0 ? A : B; // slots along the ridge
		const int M = RidgeAxis == 0 ? B : A; // slots across it
		const double U0 = RidgeAxis == 0 ? MinX : MinY; // the ridge runs from U0 to U0 + N * P
		const double V0 = RidgeAxis == 0 ? MinY : MinX;
		auto ToXY = [&](double U, double V, double& X, double& Y) { if (RidgeAxis == 0) { X = U; Y = V; } else { X = V; Y = U; } };

		std::vector<int> Tents, Sheds, Flats;

		for (int Index : Roofs)
			(T[Index].Sh == Shape::Tent ? Tents : T[Index].Sh == Shape::Shed ? Sheds : Flats).push_back(Index);

		auto CountOf = [&](const std::vector<int>& Types) { int Count = 0; for (int Index : Types) Count += Left[Index]; return Count; };
		auto NextOf = [&](const std::vector<int>& Types) { for (int Index : Types) if (Left[Index] > 0) return Index; return -1; };

		auto Caps = Of(Kind::RoofCap);
		auto NextCap = [&]() { for (int Index : Caps) if (Left[Index] > 0) return Index; return -1; };

		RoofTop = BaseZ;

		// ---- tents: a tile on every slot, the ridge along the long side; with too few tiles the rows are stretched along the ridge
		if (!Tents.empty())
		{
			const int Pieces = CountOf(Tents);
			const int FullRows = (std::max)(1, (std::min)(N, Pieces / M));
			const double Longer = (std::min)(1.5, (double)N / FullRows);
			const double RowPitch = (std::min)(N * P / FullRows, P * Longer);
			const double RowStart = U0 + (N * P - RowPitch * FullRows) / 2.0;

			for (int Along = 0; Along < FullRows; ++Along)
				for (int Across = 0; Across < M; ++Across)
				{
					const int Index = NextOf(Tents);

					if (Index < 0)
						break;

					Take(Index);
					const auto& Type = T[Index];
					double X, Y;
					ToXY(RowStart + (Along + 0.5) * RowPitch, V0 + (Across + 0.5) * P, X, Y);
					const double Yaw = (Type.RidgeAxis == RidgeAxis ? 0.0 : 90.0) + ((Along + Across) % 2 == 0 ? 0.0 : 180.0);

					// the stretch is along the tile's own ridge axis
					Put(Index, X, Y, BaseZ, Yaw, -1, Type.RidgeAxis == 0 ? Longer : 1.0, Type.RidgeAxis == 1 ? Longer : 1.0, 1.0);
					RoofTop = (std::max)(RoofTop, BaseZ + Type.SZ);
				}

			// the caps close both ends of every tent column
			for (int End = 0; End < 2; ++End)
				for (int Across = 0; Across < M; ++Across)
				{
					const int Index = NextCap();

					if (Index < 0)
						break;

					const auto& Cap = T[Index];
					Take(Index);
					const double Yaw = PickYaw(Cap, -1, 0, RidgeAxis);
					const double Thin = Cap.Short();
					double X, Y;
					ToXY(End == 0 ? U0 - Thin / 2 : U0 + N * P + Thin / 2, V0 + (Across + 0.5) * P, X, Y);
					Put(Index, X, Y, BaseZ, Yaw);
				}
		}

		// ---- sheds: pairs along the ridge. A slope is stretched so that one (or a stack of them) spans from the eave to the ridge, keeping its pitch.
		if (!Sheds.empty() && CountOf(Sheds) >= 2)
		{
			const auto& Probe = T[Sheds[0]];
			const double Reach0 = Probe.HighAxis == 0 ? Probe.SX : Probe.SY; // along the slope
			const double Rise0 = Probe.SZ * 0.92;
			const double RidgeLength = Probe.HighAxis == 0 ? Probe.SY : Probe.SX; // along the ridge
			const int Rows = (std::max)(1, (int)std::lround(N * P / RidgeLength));
			const double Span = M * P / 2.0 + 60;
			const int Pieces = CountOf(Sheds);

			int Stack = (std::max)(1, (std::min)(4, (int)std::ceil(Span / (Reach0 * 1.3))));
			int UseRows = (std::max)(1, (std::min)(Rows, Pieces / (2 * Stack)));

			// not enough pieces for every row: fewer layers (a steeper stretch) before leaving the ridge short
			while (Stack > 1 && UseRows < Rows)
			{
				const int Fewer = Stack - 1;

				if (Span / (Fewer * Reach0) > 1.7)
					break;

				Stack = Fewer;
				UseRows = (std::max)(1, (std::min)(Rows, Pieces / (2 * Stack)));
			}

			const double Stretch = (std::max)(0.55, (std::min)(1.7, Span / (Stack * Reach0))); // along the slope and up (never stretched far)
			const double Longer = (std::min)(1.5, (double)Rows / UseRows); // along the ridge, when rows are missing
			const double Reach = Reach0 * Stretch, Rise = Rise0 * Stretch;
			const double RowPitch = (std::min)(N * P / UseRows, RidgeLength * Longer);
			const double RowStart = U0 + (N * P - RowPitch * UseRows) / 2.0;

			auto Scale = [&](const Type& Type, double& Sx, double& Sy, double& Sz) {
				const int Slope = Type.Sh == Shape::Shed ? Type.HighAxis : (Type.LongAlongX() ? 0 : 1);
				Sx = Slope == 0 ? Stretch : Longer;
				Sy = Slope == 1 ? Stretch : Longer;
				Sz = Stretch;
			};

			for (int Row = 0; Row < UseRows; ++Row)
			{
				const double U = RowStart + (Row + 0.5) * RowPitch;

				for (int Side = 0; Side < 2; ++Side)
					for (int Layer = 0; Layer < Stack; ++Layer)
					{
						const int Index = NextOf(Sheds);

						if (Index < 0)
							break;

						const auto& Type = T[Index];
						Take(Index);

						const double From = Layer * Reach;
						const double Eave = Side == 0 ? V0 - 40 : V0 + M * P + 40;
						const double Middle = Side == 0 ? Eave + From + Reach / 2 : Eave - From - Reach / 2;
						const double Z = BaseZ + Layer * Rise + Layer * 2.0;
						double X, Y, Sx, Sy, Sz;
						ToXY(U, Middle, X, Y);
						Scale(Type, Sx, Sy, Sz);
						const double Yaw = PickYaw(Type, RidgeAxis == 0 ? 1 : 0, Side == 0 ? 1 : -1, -1);
						Put(Index, X, Y, Z, Yaw, -1, Sx, Sy, Sz);
						RoofTop = (std::max)(RoofTop, Z + Type.SZ * Stretch);
					}
			}

			// the caps: one for every layer at both ends of the ridge, stretched like the slope they close
			for (int End = 0; End < 2; ++End)
				for (int Side = 0; Side < 2; ++Side)
					for (int Layer = 0; Layer < Stack; ++Layer)
					{
						const int CapIndex = NextCap();

						if (CapIndex < 0)
							break;

						const auto& Cap = T[CapIndex];
						Take(CapIndex);
						const double From = Layer * Reach;
						const double Eave = Side == 0 ? V0 - 40 : V0 + M * P + 40;
						const double Middle = Side == 0 ? Eave + From + Reach / 2 : Eave - From - Reach / 2;
						const double CapYaw = PickYaw(Cap, RidgeAxis == 0 ? 1 : 0, Side == 0 ? 1 : -1, RidgeAxis);
						const double Thin = Cap.Short();
						double X, Y;
						ToXY(End == 0 ? U0 - Thin / 2 : U0 + N * P + Thin / 2, Middle, X, Y);

						// the cap's wide side lies along the slope: that is the local axis that is stretched with the height
						const int Wide = Cap.Sh == Shape::Shed ? Cap.HighAxis : (Cap.LongAlongX() ? 0 : 1);
						Put(CapIndex, X, Y, BaseZ + Layer * Rise + Layer * 2.0, CapYaw, -1, Wide == 0 ? Stretch : 1.0, Wide == 1 ? Stretch : 1.0, Stretch);
					}
		}

		// ---- flat roof pieces: laid like floors on the walls
		if (!Flats.empty())
		{
			auto Index = NextOf(Flats);

			while (Index >= 0)
			{
				const auto& Type = T[Index];
				const int Cx = (std::max)(1, (int)std::lround(A * P / (std::max)(128.0, Type.SX)));
				const int Cy = (std::max)(1, (int)std::lround(B * P / (std::max)(128.0, Type.SY)));
				const double Wx = A * P / Cx, Wy = B * P / Cy;

				for (int y = 0; y < Cy && Left[Index] > 0; ++y)
					for (int x = 0; x < Cx && Left[Index] > 0; ++x)
					{
						Take(Index);
						Put(Index, MinX + (x + 0.5) * Wx, MinY + (y + 0.5) * Wy, BaseZ, 0);
						RoofTop = (std::max)(RoofTop, BaseZ + Type.SZ);
					}

				Index = NextOf(Flats);

				if (Index >= 0 && Left[Index] > 0 && Out.Items.size() > 4000)
					break;

				// a second layer of flats would be the same cells again: stop after one pass over the footprint
				break;
			}
		}

		// ---- leftover caps: closed in at the ends as they fit
		if (NextCap() >= 0)
			Note(Fmt("%.0f roof caps did not fit anywhere", (double)LeftOf(Kind::RoofCap)));
	}

	// ---------------------------------------------------------------------------------------------------------------------
	inline void Builder::Features()
	{
		auto Tops = Of(Kind::Feature);

		if (Tops.empty())
			return;

		// a cupola or a steeple in the middle of the ridge; more of them along it
		double X = 0, Y = 0;
		int Placed = 0;

		for (int Index : Tops)
			while (Left[Index] > 0 && Placed < 4)
			{
				Take(Index);
				const double Offset = (Placed % 2 == 0 ? 1 : -1) * ((Placed + 1) / 2) * 1.5 * P;
				const double AlongX = RidgeAxis == 0 ? X + Offset : X, AlongY = RidgeAxis == 0 ? Y : Y + Offset;
				Put(Index, AlongX, AlongY, RoofTop - 30.0, 0);
				++Placed;
			}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Pieces of roof that are left (and fit on a wall) become a porch roof over the entrance.
	inline void Builder::Awning()
	{
		std::vector<int> Spare;

		for (int Index : Of(Kind::Roof))
			if (Left[Index] > 0 && T[Index].Sh == Shape::Shed)
				Spare.push_back(Index);

		if (Spare.empty() || L < 1)
			return;

		int Count = 0;

		for (int Index : Spare)
			Count += Left[Index];

		const int Wide = (std::min)(A, Count);
		const double X0 = -Wide * P / 2.0;
		int Placed = 0;

		for (int Index : Spare)
			while (Left[Index] > 0 && Placed < Wide)
			{
				const auto& Type = T[Index];
				const double Reach = Type.HighAxis == 0 ? Type.SX : Type.SY;
				Take(Index);
				// the high side against the front wall, over the first level
				const double Yaw = PickYaw(Type, 1, 1, -1);
				Put(Index, X0 + (Placed + 0.5) * P, MinY - Reach / 2 + 40, Ls * 0.85, Yaw);
				++Placed;
			}

		if (Placed > 0)
			Note(Fmt("%.0f spare roof pieces form a porch roof over the front", (double)Placed));
	}

	inline void Builder::Trims()
	{
		// battlements and caps along the top of the front, then the back
		auto Pieces = Of(Kind::Trim);

		if (Pieces.empty())
			return;

		int Slot = 0;

		for (int Index : Pieces)
		{
			const auto& Type = T[Index];

			while (Left[Index] > 0 && Slot < 2 * A)
			{
				const bool bFront = Slot < A;
				const int k = Slot % A;
				Take(Index);
				Put(Index, MinX + (k + 0.5) * P, bFront ? MinY : MaxY, L * Ls, Type.LongAlongX() ? 0.0 : 90.0, 1);
				++Slot;
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Everything else: props and what was left of the other kinds, in tidy rows in the yard (front first, then the sides and the back) and inside.
	inline void Builder::Props()
	{
		// only decoration goes in the yard (and the small structural pieces that found no place); a roof, a wall or a door piece that was not needed is left out
		std::vector<int> Rest;
		int LeftOut = 0;

		for (size_t i = 0; i < T.size(); ++i)
		{
			if (Left[i] <= 0)
				continue;

			if (T[i].K != Kind::Prop)
			{
				LeftOut += Left[i];
				continue;
			}

			Rest.push_back((int)i);
		}

		if (LeftOut > 0)
			Note(Fmt("%.0f spare structural pieces (walls, roofs, floors ...) were not needed and are left out", (double)LeftOut));

		if (Rest.empty())
			return;

		// the largest first, the same kind together
		std::stable_sort(Rest.begin(), Rest.end(), [&](int X, int Y) {
			const double AreaX = T[X].SX * T[X].SY, AreaY = T[Y].SX * T[Y].SY;
			return AreaX * std::sqrt((double)(std::min)(T[X].Count, 30)) > AreaY * std::sqrt((double)(std::min)(T[Y].Count, 30));
		});

		struct Row { double Y = 0, Depth = 0, Width = 0; std::vector<std::pair<int, double>> Items; };
		struct Zone { double X0, X1, Y0, Y1; std::vector<Row> Rows; double Used = 0; };
		struct Stack { int Type; size_t Zone; size_t Row; size_t Item; int Height; };
		std::vector<Zone> Zones;
		std::vector<Stack> StackedAt;
		const double Gap = 50;

		// the yard: the volume's footprint round the core, or a band outside it when the core fills the volume; further rings go outwards
		const double FrontDepth = (std::max)(MinY - VolMinY, 1.2 * P);
		const double SideWidth = (std::max)(MinX - VolMinX, 1.0 * P);
		const double BackDepth = (std::max)(VolMaxY - MaxY, 1.0 * P);

		// the way to the door stays clear
		double Clear = 1.1 * P;

		for (auto& Placed : Out.Items)
			if (T[Placed.Type].K == Kind::Door)
				Clear = (std::max)(Clear, T[Placed.Type].Long() / 2 + 250.0);

		auto Ring = [&](int Round) {
			const double Out_ = Round * 140.0;
			const double Front0 = MinY - 100 - FrontDepth - Round * FrontDepth - Out_;
			const double Front1 = Front0 + FrontDepth;
			const double East0 = MaxX + 100 + Round * SideWidth + Out_, West1 = MinX - 100 - Round * SideWidth - Out_;
			const double Back0 = MaxY + 100 + Round * BackDepth + Out_;
			const double WideX0 = (std::min)(VolMinX, MinX - SideWidth) - Round * (SideWidth + 140), WideX1 = (std::max)(VolMaxX, MaxX + SideWidth) + Round * (SideWidth + 140);

			Zones.push_back({ WideX0, -Clear, Front0, Front1 });
			Zones.push_back({ Clear, WideX1, Front0, Front1 });
			Zones.push_back({ East0, East0 + SideWidth, MinY, MaxY });
			Zones.push_back({ West1 - SideWidth, West1, MinY, MaxY });
			Zones.push_back({ WideX0, WideX1, Back0, Back0 + BackDepth });
		};

		Ring(0);

		// inside: the aisle between the arcades, else everything inside the walls
		if (A >= 2 && B >= 2)
		{
			if (PartitionPieces > 0)
			{
				const double Half = 0.38 * P;

				if (A > B)
					Zones.push_back({ MinX + 0.6 * P, MaxX - 0.6 * P, -Half, Half });
				else
					Zones.push_back({ -Half, Half, MinY + 0.6 * P, MaxY - 0.6 * P });
			}
			else
				Zones.push_back({ MinX + 0.7 * P, MaxX - 0.7 * P, MinY + 0.6 * P, MaxY - 0.6 * P });
		}

		Ring(1);
		Ring(2);

		auto TryAdd = [&](Zone& Z, int Index, double W, double D) -> bool {
			const double ZoneWidth = Z.X1 - Z.X0, ZoneDepth = Z.Y1 - Z.Y0;

			if (W > ZoneWidth || D > ZoneDepth)
				return false;

			if (!Z.Rows.empty())
			{
				auto& Last = Z.Rows.back();

				if (Last.Width + Gap + W <= ZoneWidth && (std::max)(Last.Depth, D) + Last.Y <= ZoneDepth)
				{
					Last.Items.push_back({ Index, W });
					Last.Width += Gap + W;
					Last.Depth = (std::max)(Last.Depth, D);
					Z.Used = Last.Y + Last.Depth;
					return true;
				}
			}

			const double Y = Z.Rows.empty() ? 0.0 : Z.Used + Gap;

			if (Y + D > ZoneDepth)
				return false;

			Row New;
			New.Y = Y; New.Depth = D; New.Width = W;
			New.Items.push_back({ Index, W });
			Z.Rows.push_back(New);
			Z.Used = Y + D;
			return true;
		};

		int Dropped = 0;
		const size_t FirstOuter = Zones.size() > 11 ? 6 : Zones.size(); // the first ring and the inside come before the outer rings
		struct Spot { int Type; size_t Zone; size_t Row; size_t Item; int Height; };
		std::vector<Spot> Spots;

		for (int Index : Rest)
		{
			while (Left[Index] > 0)
			{
				bool bPlaced = false;

				for (size_t z = 0; z < Zones.size() && !bPlaced; ++z)
				{
					if (z >= FirstOuter && (T[Index].SZ <= 160 && T[Index].Count >= 4))
						break; // small things stack before they spread to the outer rings

					if (TryAdd(Zones[z], Index, T[Index].SX, T[Index].SY))
					{
						Spots.push_back({ Index, z, Zones[z].Rows.size() - 1, Zones[z].Rows.back().Items.size() - 1, 1 });
						bPlaced = true;
					}
				}

				if (!bPlaced && T[Index].SZ <= 160)
				{
					// on top of a piece of the same kind that is only one or two high
					for (auto& S : Spots)
						if (S.Type == Index && S.Height < 3)
						{
							++S.Height;
							StackedAt.push_back({ Index, S.Zone, S.Row, S.Item, S.Height });
							bPlaced = true;
							break;
						}
				}

				for (size_t z = FirstOuter; z < Zones.size() && !bPlaced; ++z)
					if (TryAdd(Zones[z], Index, T[Index].SX, T[Index].SY))
					{
						Spots.push_back({ Index, z, Zones[z].Rows.size() - 1, Zones[z].Rows.back().Items.size() - 1, 1 });
						bPlaced = true;
					}

				if (!bPlaced)
				{
					Dropped += Left[Index];
					Left[Index] = 0;
					break;
				}

				--Left[Index];
			}
		}

		for (size_t z = 0; z < Zones.size(); ++z)
			for (size_t r = 0; r < Zones[z].Rows.size(); ++r)
			{
				auto& Z = Zones[z];
				auto& Row = Z.Rows[r];
				double X = (Z.X0 + Z.X1) / 2 - Row.Width / 2;

				for (size_t i = 0; i < Row.Items.size(); ++i)
				{
					auto& Item = Row.Items[i];
					const double CenterX = X + Item.second / 2, CenterY = Z.Y0 + Row.Y + Row.Depth / 2;
					Put(Item.first, CenterX, CenterY, 0, 0);

					for (auto& Stack : StackedAt)
						if (Stack.Zone == z && Stack.Row == r && Stack.Item == i)
							Put(Item.first, CenterX, CenterY, (Stack.Height - 1) * T[Item.first].SZ, 0);

					X += Item.second + Gap;
				}
			}

		if (Dropped > 0)
			Note(Fmt("%.0f pieces found no room", (double)Dropped));
	}

	// ---------------------------------------------------------------------------------------------------------------------
	inline Result Builder::Run()
	{
		RefineKinds();

		const bool bGallery = Has(R.Name, "gallery");
		const int BuildingPieces = Total(Kind::Wall) + Total(Kind::Window) + Total(Kind::Opening) + Total(Kind::Door) + Total(Kind::Roof) + Total(Kind::Floor);

		if (bGallery || BuildingPieces == 0)
		{
			Exhibit();
			Out.Types = T;
			return Out;
		}

		ARCHITECT_STAGE("ChooseGrid");
		ChooseGrid();
		ARCHITECT_STAGE("BuildRing");
		BuildRing();

		Out.CoreX = A; Out.CoreY = B; Out.Levels = L; Out.Pitch = P; Out.Step = Ls;
		Out.MinX = MinX; Out.MaxX = MaxX; Out.MinY = MinY; Out.MaxY = MaxY;

		ARCHITECT_STAGE("Facade");
		Facade();
		ARCHITECT_STAGE("Partitions");
		Partitions();
		ARCHITECT_STAGE("Pillars");
		Pillars();
		ARCHITECT_STAGE("Floors");
		Floors();
		ARCHITECT_STAGE("StairsStage");
		StairsStage();
		ARCHITECT_STAGE("Rails");
		Rails();
		ARCHITECT_STAGE("Roof");
		Roof();
		ARCHITECT_STAGE("Features");
		Features();
		ARCHITECT_STAGE("Awning");
		Awning();
		ARCHITECT_STAGE("Trims");
		Trims();
		ARCHITECT_STAGE("Props");
		Props();

		int Unused = 0;

		for (size_t i = 0; i < T.size(); ++i)
			Unused += Left[i];

		if (Unused > 0)
			Note(Fmt("%.0f pieces were not placed", (double)Unused));

		Out.Types = T;
		return Out;
	}

	inline Result Plan(const Request& Req)
	{
		Builder B(Req);
		return B.Run();
	}
}
