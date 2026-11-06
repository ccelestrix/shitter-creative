// Runs the prefab layout (PrefabArchitect.h) on a description of a prefab and writes the result as JSON, for render.py to draw.
//
//   preview <description.txt> <out.json>
//
// The description is what the game writes to the creative log for every prefab it builds ("Architect input" lines), one line each:
//   prefab <name> <sizeX> <sizeY> <sizeZ>
//   type <name> <kind|auto> <count> <sx> <sy> <sz> <box|shed|tent> <highAxis> <highSign> <ridgeAxis>
// kind is one of floor roof roofcap stairs door window opening wall rail pillar trim feature prop, or auto (taken from the name).

#include "../../PrefabArchitect.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

static Architect::Kind KindFromToken(const std::string& Token, const std::string& Name)
{
	using Architect::Kind;
	static const std::pair<const char*, Kind> Map[] = { { "floor", Kind::Floor }, { "roof", Kind::Roof }, { "roofcap", Kind::RoofCap }, { "stairs", Kind::Stairs }, { "door", Kind::Door },
		{ "window", Kind::Window }, { "opening", Kind::Opening }, { "wall", Kind::Wall }, { "rail", Kind::Rail }, { "pillar", Kind::Pillar }, { "trim", Kind::Trim },
		{ "feature", Kind::Feature }, { "prop", Kind::Prop } };

	for (auto& Entry : Map)
		if (Token == Entry.first)
			return Entry.second;

	return Architect::KindFromName(Name);
}

static std::string Escape(const std::string& Text)
{
	std::string Out;

	for (char C : Text)
		if (C == '"' || C == '\\')
			Out += std::string("\\") + C;
		else
			Out += C;

	return Out;
}

#include <chrono>
#include <random>

// preview --fuzz <count>: random prefabs through the layout, to see that it never crashes, never takes long and never returns nonsense.
static int Fuzz(int Count)
{
	std::mt19937 Random(12345);
	auto Pick = [&](int Lo, int Hi) { return (int)(Random() % (unsigned)(Hi - Lo + 1)) + Lo; };
	auto Real = [&](double Lo, double Hi) { return Lo + (Hi - Lo) * (Random() % 100000) / 100000.0; };
	const char* Names[] = { "SM_Wall_A", "Window_B", "Arch_Door", "Roof_Tile_S", "Roof_Cap_B", "Floor_Boardwalk", "Stairs_A", "Pillar_Stone", "Door_Large_Base", "Door_Large_Top", "Steeple", "Rail_Wood", "Prop_Crate",
		"Haybale", "Barrel", "Trim_Cap", "Fence_Post", "Ceiling_A", "Window_Round", "Chair" };
	double Slowest = 0;
	int Failures = 0;

	for (int Iteration = 0; Iteration < Count; ++Iteration)
	{
		Architect::Request R;
		R.Name = Pick(0, 9) == 0 ? "Gallery_Test" : "Fuzz";
		R.SizeX = Pick(1, 14); R.SizeY = Pick(1, 14); R.SizeZ = Pick(1, 9);
		const int Types = Pick(0, 30);

		for (int i = 0; i < Types; ++i)
		{
			Architect::Type T;
			T.Name = Names[Pick(0, 19)];
			T.K = Pick(0, 4) == 0 ? (Architect::Kind)Pick(0, 12) : Architect::KindFromName(T.Name);
			T.Count = Pick(0, 3) == 0 ? Pick(1, 150) : Pick(1, 12);
			T.SX = Real(10, 1400); T.SY = Real(10, 1400); T.SZ = Real(5, 900);

			if (Pick(0, 2) == 0) { T.SX = Pick(0, 1) ? 512 : T.SX; T.SZ = Pick(0, 1) ? 384 : T.SZ; }

			const int Shape = Pick(0, 2);
			T.Sh = Shape == 0 ? Architect::Shape::Box : Shape == 1 ? Architect::Shape::Shed : Architect::Shape::Tent;
			T.HighAxis = Pick(0, 1); T.HighSign = Pick(0, 1) ? 1 : -1; T.RidgeAxis = Pick(0, 1);
			R.Types.push_back(T);
		}

		if (std::getenv("FUZZ_VERBOSE"))
		{
			std::printf("iteration %d: %d x %d x %d, %zu types\n", Iteration, R.SizeX, R.SizeY, R.SizeZ, R.Types.size());
			std::fflush(stdout);

			for (auto& T : R.Types)
				std::printf("  type %s kind %s count %d box %.0f %.0f %.0f shape %d\n", T.Name.c_str(), Architect::KindToken(T.K), T.Count, T.SX, T.SY, T.SZ, (int)T.Sh);

			std::fflush(stdout);
		}

		const auto Start = std::chrono::steady_clock::now();
		auto Result = Architect::Plan(R);
		const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
		Slowest = (std::max)(Slowest, Seconds);
		bool bBad = Result.Items.size() > 5000;

		for (auto& Item : Result.Items)
		{
			if (Item.Type < 0 || Item.Type >= (int)R.Types.size() || !std::isfinite(Item.X) || !std::isfinite(Item.Y) || !std::isfinite(Item.Z) || !std::isfinite(Item.Yaw) ||
				std::fabs(Item.X) > 60000 || std::fabs(Item.Y) > 60000 || Item.Z < -1 || Item.Z > 40000 || Item.ScaleX < 0.2 || Item.ScaleX > 4 || Item.ScaleY < 0.2 || Item.ScaleY > 4 || Item.ScaleZ < 0.2 || Item.ScaleZ > 4)
			{
				bBad = true;
				std::printf("iteration %d: bad item type %d at %.0f %.0f %.0f scale %.2f %.2f %.2f\n", Iteration, Item.Type, Item.X, Item.Y, Item.Z, Item.ScaleX, Item.ScaleY, Item.ScaleZ);
				break;
			}
		}

		// no piece may be placed more often than the prefab lists it
		std::vector<int> Used(R.Types.size(), 0);

		for (auto& Item : Result.Items)
			if (Item.Type >= 0 && Item.Type < (int)R.Types.size())
				++Used[Item.Type];

		for (size_t i = 0; i < R.Types.size(); ++i)
			if (Used[i] > R.Types[i].Count)
			{
				bBad = true;
				std::printf("iteration %d: type %zu (%s) placed %d times, only %d listed\n", Iteration, i, R.Types[i].Name.c_str(), Used[i], R.Types[i].Count);
				break;
			}

		if (bBad || Seconds > 0.5)
		{
			++Failures;
			std::printf("iteration %d failed (%.3f s, %zu pieces, size %d x %d x %d, %zu types)\n", Iteration, Seconds, Result.Items.size(), R.SizeX, R.SizeY, R.SizeZ, R.Types.size());
		}
	}

	std::printf("fuzz: %d prefabs, %d failures, slowest %.4f s\n", Count, Failures, Slowest);
	return Failures ? 1 : 0;
}

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

int main(int Argc, char** Argv)
{
#ifdef _MSC_VER
	// a failed check prints a line and stops, no dialog
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

	if (Argc >= 3 && std::string(Argv[1]) == "--fuzz")
		return Fuzz(std::atoi(Argv[2]));

	if (Argc < 3)
	{
		std::fprintf(stderr, "usage: preview <description.txt> <out.json>\n");
		return 1;
	}

	std::ifstream In(Argv[1]);
	Architect::Request R;
	std::string Line;

	while (std::getline(In, Line))
	{
		std::istringstream S(Line);
		std::string Word;
		S >> Word;

		if (Word == "prefab")
			S >> R.Name >> R.SizeX >> R.SizeY >> R.SizeZ;
		else if (Word == "type")
		{
			Architect::Type T;
			std::string Kind, Shape;
			int HighAxis = 0, HighSign = 0, RidgeAxis = 0;
			S >> T.Name >> Kind >> T.Count >> T.SX >> T.SY >> T.SZ >> Shape >> HighAxis >> HighSign >> RidgeAxis;
			T.K = KindFromToken(Kind, T.Name);
			T.Sh = Shape == "shed" ? Architect::Shape::Shed : Shape == "tent" ? Architect::Shape::Tent : Architect::Shape::Box;
			T.HighAxis = HighAxis; T.HighSign = HighSign; T.RidgeAxis = RidgeAxis;
			T.Measured = true;
			R.Types.push_back(T);
		}
	}

	auto Result = Architect::Plan(R);

	std::ofstream Out(Argv[2]);
	Out << "{\"name\":\"" << Escape(R.Name) << "\",\"size\":[" << R.SizeX << "," << R.SizeY << "," << R.SizeZ << "],";
	Out << "\"exhibit\":" << (Result.bExhibit ? "true" : "false") << ",\"core\":[" << Result.CoreX << "," << Result.CoreY << "," << Result.Levels << "],";
	Out << "\"pitch\":" << Result.Pitch << ",\"step\":" << Result.Step << ",\"bounds\":[" << Result.MinX << "," << Result.MinY << "," << Result.MaxX << "," << Result.MaxY << "],";
	Out << "\"types\":[";

	for (size_t i = 0; i < Result.Types.size(); ++i)
	{
		auto& T = Result.Types[i];
		Out << (i ? "," : "") << "{\"name\":\"" << Escape(T.Name) << "\",\"kind\":\"" << Architect::KindName(T.K) << "\",\"count\":" << T.Count << ",\"size\":[" << T.SX << "," << T.SY << "," << T.SZ << "],\"shape\":\""
			<< (T.Sh == Architect::Shape::Shed ? "shed" : T.Sh == Architect::Shape::Tent ? "tent" : "box") << "\",\"high\":[" << T.HighAxis << "," << T.HighSign << "],\"ridge\":" << T.RidgeAxis << "}";
	}

	Out << "],\"items\":[";

	for (size_t i = 0; i < Result.Items.size(); ++i)
	{
		auto& P = Result.Items[i];
		Out << (i ? "," : "") << "{\"t\":" << P.Type << ",\"x\":" << P.X << ",\"y\":" << P.Y << ",\"z\":" << P.Z << ",\"yaw\":" << P.Yaw << ",\"scale\":[" << P.ScaleX << "," << P.ScaleY << "," << P.ScaleZ << "]}";
	}

	Out << "],\"notes\":[";

	for (size_t i = 0; i < Result.Notes.size(); ++i)
		Out << (i ? "," : "") << "\"" << Escape(Result.Notes[i]) << "\"";

	Out << "]}\n";

	std::printf("%s: %zu pieces placed, core %d x %d, %d levels, pitch %.0f, level %.0f\n", R.Name.c_str(), Result.Items.size(), Result.CoreX, Result.CoreY, Result.Levels, Result.Pitch, Result.Step);

	for (auto& Note : Result.Notes)
		std::printf("  note: %s\n", Note.c_str());

	return 0;
}
