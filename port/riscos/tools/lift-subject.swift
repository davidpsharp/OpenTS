// Cuts the subject out of a picture, leaving the background transparent, with macOS's
// Vision framework (the same as "Remove Background" in Preview). macOS 14 or later.
//   swift lift-subject.swift <in.png> <out.png>
import Foundation
import Vision
import CoreImage
import CoreImage.CIFilterBuiltins
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
let input = URL(fileURLWithPath: args[1]), output = URL(fileURLWithPath: args[2])
guard let image = CIImage(contentsOf: input) else { print("can't read"); exit(1) }
let handler = VNImageRequestHandler(ciImage: image)
let request = VNGenerateForegroundInstanceMaskRequest()
try handler.perform([request])
guard let result = request.results?.first else { print("no subject found"); exit(1) }
print("instances:", result.allInstances.count)
let mask = try result.generateScaledMaskForImage(forInstances: result.allInstances, from: handler)
let maskImage = CIImage(cvPixelBuffer: mask)
let blend = CIFilter.blendWithMask()
blend.inputImage = image
blend.maskImage = maskImage
blend.backgroundImage = CIImage.empty()
let context = CIContext()
let out = blend.outputImage!
guard let cg = context.createCGImage(out, from: image.extent) else { exit(1) }
let dest = CGImageDestinationCreateWithURL(output as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dest, cg, nil)
CGImageDestinationFinalize(dest)
print("written")
