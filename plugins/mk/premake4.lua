newoption {
	trigger		= "with-mk",
	description	= "Enable the Minko MK plugin."
}

minko.project.library "plugin-mk"
	kind "StaticLib"
	language "C++"
	files {
		"src/**.hpp",
		"src/**.h",
		"src/**.cpp",
		"src/**.c"
		-- "lib/msgpack-c/src/**.cpp",
		-- "lib/msgpack-c/src/**.h"
	}
	includedirs {
		"include",
		"src",
		"lib/msgpack-c/src"
	}

	configuration { "windows" }
		-- msgpack
		defines {
			"_LIB",
			"_CRT_SECURE_NO_WARNINGS",
			"_CRT_SECURE_NO_DEPRECATE",
			"__STDC_VERSION__=199901L",
			"__STDC__",
			"WIN32"
		}
		buildoptions {
			"/wd4028",
			"/wd4244",
			"/wd4267",
			"/wd4996",
			"/wd4273",
			"/wd4503"
		}

	configuration { "linux" }
		buildoptions {
			"-Wno-deprecated-declarations"
		}
