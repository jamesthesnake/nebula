//------------------------------------------------------------------------------
//  testflatc/main.cc
//  (C) 2018 Individual contributors, see AUTHORS file
//------------------------------------------------------------------------------
#include "stdneb.h"
#include "foundation.h"
#include "core/coreserver.h"
#include "testbase/testrunner.h"
#include "flatctest.h"


using namespace Core;
using namespace Test;

void
__cdecl main()
{
    // create Nebula runtime
    Ptr<CoreServer> coreServer = CoreServer::Create();
    coreServer->SetAppName("Nebula flatc Tests");
    coreServer->Open();

    n_printf("NEBULA flatc TESTS\n");
    n_printf("========================\n");

    // setup and run test runner
    Ptr<TestRunner> testRunner = TestRunner::Create();
    testRunner->AttachTestCase(FlatCTest::Create());    
    testRunner->Run(); 

    coreServer->Close();
    testRunner = nullptr;
    coreServer = nullptr;
    
    Core::SysFunc::Exit(0);
}
