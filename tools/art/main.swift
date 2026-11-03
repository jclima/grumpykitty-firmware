// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// Entry point. Usage:
//   draw <outdir> [<reference_dir>]      v1 kawaii sampler (make_preview.sh)
//   draw v2 <outdir> [<reference_dir>]   v2 grumpy kitty set  (make_v2.sh)

import Foundation

let args = CommandLine.arguments
guard args.count >= 2 else { print("usage: draw [v2] <outdir> [<reference_dir>]"); exit(1) }
if args[1] == "v2" {
    guard args.count >= 3 else { print("usage: draw v2 <outdir> [<reference_dir>]"); exit(1) }
    runV2(args[2], args.count > 3 ? args[3] : args[2] + "/../reference")
} else {
    runV1(args[1], args.count > 2 ? args[2] : args[1] + "/reference")
}
