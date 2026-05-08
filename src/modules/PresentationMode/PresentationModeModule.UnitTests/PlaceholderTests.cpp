// Suppressing 26466 - Don't use static_cast downcasts - in CppUnitTest.h
#pragma warning(push)
#pragma warning(disable : 26466)
#include "CppUnitTest.h"
#pragma warning(pop)

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace PresentationModeUnitTests
{
    TEST_CLASS(PlaceholderTests)
    {
    public:
        TEST_METHOD(SanityCheck)
        {
            Assert::IsTrue(true, L"Sanity check passed.");
        }
    };
}
