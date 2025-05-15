#include <gtk/gtk.h>
#include <zlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include "png.h"

#define PNG_SIG_BYTES 8

/**
 * Global image widget
 */
GtkWidget *image_widget = NULL;

/**
 * Custom check to know if a png contains an iCCP chunk or not
 */
int cus_png_get_iCCP(png_const_structrp png_ptr,
    png_inforp info_ptr,
    png_charpp name,
    int *compression_type,
    png_bytepp profile,
    png_uint_32 *proflen
){
    FILE *fp = (FILE *)png_get_io_ptr(png_ptr);
    if (!fp) return 0;

    long original_pos = ftell(fp);
    fseek(fp, 8, SEEK_SET); // Skip PNG signature

    while (!feof(fp)) {
        unsigned char lenbuf[4], type[4];
        if (fread(lenbuf, 1, 4, fp) != 4 || fread(type, 1, 4, fp) != 4)
            break;

        uint32_t len = (lenbuf[0] << 24) | (lenbuf[1] << 16) |
                       (lenbuf[2] << 8) | lenbuf[3];

        if (memcmp(type, "iCCP", 4) == 0) {
            png_bytep chunk_data = (png_bytep)malloc(len);
            if (!chunk_data || fread(chunk_data, 1, len, fp) != len) {
                free(chunk_data);
                break;
            }
            fseek(fp, 4, SEEK_CUR); // Skip CRC

            size_t keyword_len = 0;
            while (keyword_len < len && chunk_data[keyword_len] != '\0') keyword_len++;

            if (keyword_len >= len - 2) {
                free(chunk_data);
                break;
            }

            printf("Contains an iCCP chunk\n");

            free(chunk_data);
            fseek(fp, original_pos, SEEK_SET);
            return 1;
        }

        fseek(fp, len + 4, SEEK_CUR);
    }

    fseek(fp, original_pos, SEEK_SET);
    return 0;
}


/**
 * Load a PNG using libpng and convert it to a GdkPixbuf
 */
static GdkPixbuf* load_png_with_libpng(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        return NULL;
    }

    /**
     * MARK: Read
     */
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "libpng error while reading file\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }


    png_byte sig[PNG_SIG_BYTES];
    fread(sig, 1, PNG_SIG_BYTES, fp);
    
    if (png_sig_cmp(sig, 0, PNG_SIG_BYTES) == 0) {
        printf("Signature OK\n");
    } else {
        printf("Invalid Signature\n");
        fclose(fp);
        return NULL;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, PNG_SIG_BYTES);
    png_read_info(png_ptr, info_ptr);


    /**
     * MARK: Write
     */
    FILE *tmp = tmpfile();
    png_structp write_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!write_ptr) {
        fclose(tmp);
        return NULL;
    }

    png_infop write_info = png_create_info_struct(write_ptr);
    if (!write_info) {
        png_destroy_write_struct(&write_ptr, NULL);
        fclose(tmp);
        return NULL;
    }

    if (setjmp(png_jmpbuf(write_ptr))) {
        fprintf(stderr, "libpng error while writing file\n");
        png_destroy_write_struct(&write_ptr, &write_info);
        fclose(tmp);
        return NULL;
    }

    /**
     * MARK: Vulnerability
     * Extract the iCCP profile and try to reuse it during a write to trigger potential bug
     */
    // if (png_get_valid(png_ptr, info_ptr, PNG_INFO_iCCP)) {
    //     printf("iCCP chunk is valid and parsed\n");
    // } else {
    //     printf("iCCP chunk was rejected by libpng\n");
    // }
    char *name = NULL;
    int compression = 0;
    png_bytep profile = NULL;
    png_uint_32 len = 0;

    // png_get_iCCP(png_ptr, info_ptr, &name, &compression, &profile, &len);
    // png_byte fake_profile[3] = { 0xde, 0xad, 0xbe };  // fake length = 0xdeadbe??
    // printf("fake_profile_len (from profile[0..3]): %u\n", png_get_uint_32(fake_profile));
    // printf("profile_len: %u\n", png_get_uint_32(profile));
    // printf("profile_len: %u\n", len);
    if (cus_png_get_iCCP(png_ptr, info_ptr, &name, &compression, &profile, &len)) {
        png_get_iCCP(png_ptr, info_ptr, &name, &compression, &profile, &len);
        printf("Found iCCP chunk: profile_len=%u\n", len);
        if (tmp) {
            png_init_io(write_ptr, tmp);
            png_set_IHDR(write_ptr, write_info, 1, 1, 8, PNG_COLOR_TYPE_RGB,
                         PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
            // printf("iCCP chunk: profile_len=%u\n", len);
            // printf("iCCP chunk: png_get_uint_32(profile)=%u\n", png_get_uint_32(profile));
            // const char profile_name[] = "sRGB";
            // const png_charp profile_data = (png_charp)"ABC";
            // png_uint_32 profile_len = 3;
            // int compression_type = 0;
            png_set_iCCP(write_ptr, write_info, name, compression, profile, len);
            png_write_info(write_ptr, write_info);
            fclose(tmp);
        }
        png_destroy_write_struct(&write_ptr, &write_info);    
    }

    /**
     * MARK: Display
     */
    png_uint_32 width, height;
    int bit_depth, color_type;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth,
                 &color_type, NULL, NULL, NULL);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    int rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    png_byte *image_data = malloc(rowbytes * height);
    if (!image_data) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    png_bytep *row_pointers = malloc(sizeof(png_bytep) * height);
    for (size_t i = 0; i < height; i++)
        row_pointers[i] = image_data + i * rowbytes;

    png_read_image(png_ptr, row_pointers);

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(
        image_data,
        GDK_COLORSPACE_RGB,
        png_get_channels(png_ptr, info_ptr) == 4,
        8,
        width,
        height,
        rowbytes,
        (GdkPixbufDestroyNotify)free,
        NULL
    );

    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
    return pixbuf;
}

/**
 * Callback func to open a file dialog handler
 */
static void on_open_file(GtkWidget *widget, gpointer user_data) {
    GtkWidget *dialog;
    GtkWidget *parent_window = GTK_WIDGET(user_data);

    dialog = gtk_file_chooser_dialog_new("Open PNG Image",
                                         GTK_WINDOW(parent_window),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT,
                                         NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        GdkPixbuf *pixbuf = load_png_with_libpng(filename);
        if (pixbuf) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(image_widget), pixbuf);
            g_object_unref(pixbuf);
        } else {
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(parent_window),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Failed to load PNG file.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "PNG Viewer (libpng)");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 200);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *button = gtk_button_new_with_label("Open PNG");
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_open_file), window);

    image_widget = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(vbox), image_widget, TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}