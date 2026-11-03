// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// Entry point for the v3 sampler. Usage: draw_v3 <outdir>   (make_v3.sh)

import Foundation

@main
struct V3Main {
    static func main() {
        let args = CommandLine.arguments
        guard args.count >= 2 else { print("usage: draw_v3 <outdir>"); exit(1) }
        runV3(args[1])
    }
}
