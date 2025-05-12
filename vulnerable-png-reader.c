#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

#include "png.h"

static void on_open_file(GtkWidget *widget, gpointer user_data) {
    GtkWidget *dialog;
    GtkWidget *window = GTK_WIDGET(user_data);

    dialog = gtk_file_chooser_dialog_new("Open PNG Image",
        GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        FILE *fp = fopen(filename, "rb");
        if (!fp) {
            perror("fopen");
            gtk_widget_destroy(dialog);
            return;
        }

        png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png_ptr) return;

        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr) return;

        if (setjmp(png_jmpbuf(png_ptr))) {
            fprintf(stderr, "libpng error\n");
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            fclose(fp);
            return;
        }

        png_init_io(png_ptr, fp);
        png_read_info(png_ptr, info_ptr);

        // Load image just to trigger chunk parsing (e.g., iCCP)
        png_read_image(png_ptr, NULL); // Will crash if png is malformed

        fclose(fp);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "PNG Loader");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 100);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *button = gtk_button_new_with_label("Load PNG");
    g_signal_connect(button, "clicked", G_CALLBACK(on_open_file), window);
    gtk_container_add(GTK_CONTAINER(window), button);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
