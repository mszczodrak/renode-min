﻿//
// Copyright (c) 2010-2019 Antmicro
//
//  This file is licensed under the MIT License.
//  Full license text is available in 'licenses/MIT.txt'.
//
namespace Antmicro.Renode.Utilities
{
    public static class MockExtension
    {
        public static string GetMockString(this IInterestingType str)
        {
            return "this is an extension";
        }
    }
}
