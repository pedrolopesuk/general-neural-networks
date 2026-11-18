// Compiles the untouched standard C++20 MetaBrain engine sources inside MetaBrainCore.
// Guards Unreal's lowercase check/verify/PI macros before including standard C++ headers.

#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("PI")
#undef check
#undef verify
#undef PI

#include "../../../src/Neuron.cpp"
#include "../../../src/SensoryPipeline.cpp"
#include "../../../src/BrainEngine.cpp"
#include "../../../src/Layout3D.cpp"
#include "../../../src/Serializer.cpp"

#pragma pop_macro("PI")
#pragma pop_macro("verify")
#pragma pop_macro("check")
