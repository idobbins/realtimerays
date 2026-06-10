#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

int main(int argc, char **argv)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSString *src = [NSString stringWithContentsOfFile:@(argv[1])
                                                  encoding:NSUTF8StringEncoding
                                                     error:nil];
        NSError *err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
        if (!lib) { fprintf(stderr, "compile: %s\n", err.description.UTF8String); return 1; }
        id<MTLFunction> fn = [lib newFunctionWithName:@"main0"];
        id<MTLComputePipelineState> pso =
            [dev newComputePipelineStateWithFunction:fn error:&err];
        if (!pso) { fprintf(stderr, "pso: %s\n", err.description.UTF8String); return 1; }
        printf("maxTotalThreadsPerThreadgroup: %lu\n",
               (unsigned long)pso.maxTotalThreadsPerThreadgroup);
        printf("threadExecutionWidth: %lu\n",
               (unsigned long)pso.threadExecutionWidth);
        printf("staticThreadgroupMemoryLength: %lu\n",
               (unsigned long)pso.staticThreadgroupMemoryLength);
    }
    return 0;
}
