// Optional local-build utility: apply Finder custom icons to files and app bundles.
#import <AppKit/AppKit.h>
#include <stdio.h>

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        if (argc < 3) {
            fprintf(stderr, "Usage: nya_set_file_icon icon.icns file [file ...]\n");
            return 1;
        }
        NSString *iconPath = [[NSString alloc] initWithUTF8String:argv[1]];
        NSImage *image = [[NSImage alloc] initWithContentsOfFile:iconPath];
        if (!image || !image.valid) {
            fprintf(stderr, "Cannot read icon: %s\n", argv[1]);
            return 2;
        }
        NSWorkspace *workspace = NSWorkspace.sharedWorkspace;
        for (int i = 2; i < argc; ++i) {
            NSString *path = [[NSString alloc] initWithUTF8String:argv[i]];
            if (![workspace setIcon:image forFile:path options:0]) {
                fprintf(stderr, "Cannot apply Finder icon: %s\n", argv[i]);
                return 3;
            }
            printf("Updated Finder icon: %s\n", argv[i]);
        }
    }
    return 0;
}
