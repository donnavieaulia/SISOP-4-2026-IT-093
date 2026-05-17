# Soal 1 save asisten KENZ

Soal ini adalah implementasi **Sistem Berkas Virtual** menggunakan framework **FUSE (Filesystem in Userspace)** dalam bahasa C. Sistem ini dirancang untuk membaca jurnal perjalanan 7 hari secara dinamis, menyaring fragmen koordinat yang tersebar, dan menggabungkannya menjadi satu informasi utuh di dalam sebuah file virtual bernama `tujuan.txt`.

---


* **`kenz_rescue.c`**: Source code utama yang mengimplementasikan operasi FUSE (*handling* sistem berkas virtual).
* **`1.txt` s/d `7.txt`**: File log harian (berada di direktori sumber/asal) yang berisi narasi perjalanan Amba dan potongan koordinat dengan format `KOORD: [fragmen]`.


---



Program ini bekerja dengan cara memetakan direktori fisik (*source directory*) ke sebuah direktori virtual (*mount point*). Di dalam direktori virtual tersebut, FUSE akan menyisipkan satu file gaib/virtual bernama `tujuan.txt` yang tidak ada di harddisk asli, melainkan dibuat langsung di memori (*on-the-fly*).

Berikut adalah detail teknis dari tiap fungsi (operasi FUSE) yang diimplementasikan di `kenz_rescue.c`:

```c
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>

static char source_dir[1024];
```

### 1. Manajemen Metadata (`x_getattr`)
Fungsi ini bertugas memberikan informasi atribut (seperti tipe file, permission, dan ukuran) kepada sistem operasi.
* **Logika Berkas Virtual**: Jika sistem operasi meminta atribut untuk file `/tujuan.txt`, fungsi ini langsung memanipulasi strukturnya dengan memberikan mode `S_IFREG | 0444` (artinya: file reguler dengan akses *read-only*) dan ukuran statis sebesar `100` bytes.
* **Logika Berkas Fisik**: Jika yang diminta adalah file lain (seperti file `1.txt` dst), fungsi akan mengarahkannya ke file asli di direktori sumber menggunakan fungsi `lstat()`.

```c
static int x_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    // LOGIKA FILE VIRTUAL
    if (strcmp(path, "/tujuan.txt") == 0)
    {
        stbuf->st_mode = S_IFREG | 0444; // Berkas Reguler & Read-Only
        stbuf->st_nlink = 1;
        stbuf->st_size = 100;            // Alokasi ukuran statis di memori
        return 0;
    }

    // LOGIKA FILE FISIK (ASLI)
    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    if (lstat(fullpath, stbuf) == -1)
        return -errno;

    return 0;
}
```

### 2. Pembacaan Direktori (`x_readdir`)
Fungsi ini berjalan saat kamu melakukan perintah seperti `ls` di dalam *mount point*.
* Fungsi akan membaca seluruh isi dari direktori sumber asli menggunakan `opendir()` dan `readdir()`.
* Secara otomatis, fungsi ini menyelipkan entri baru bernama `"tujuan.txt"` ke dalam buffer menggunakan fungsi `filler()`. Hal ini membuat file virtual tersebut terlihat seolah-olah ada di dalam folder.

```c
static int x_readdir(const char *path,
                     void *buf,
                     fuse_fill_dir_t filler,
                     off_t offset,
                     struct fuse_file_info *fi)
{
    (void) offset; (void) fi; // Mengabaikan parameter yang tidak dipakai

    DIR *dp;
    struct dirent *de;

    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    dp = opendir(fullpath);
    if (dp == NULL) return -errno;

    // Membaca file asli yang ada di direktori sumber
    while ((de = readdir(dp)) != NULL) {
        filler(buf, de->d_name, NULL, 0);
    }
    closedir(dp);

    // Menyuntikkan file virtual ke root direktori (/)
    if (strcmp(path, "/") == 0) {
        filler(buf, "tujuan.txt", NULL, 0);
    }

    return 0;
}
```

### 3. Pembukaan Berkas (`x_open`)
* Memastikan operasi *open* diizinkan. Karena `/tujuan.txt` adalah file virtual *read-only*, fungsi ini memastikan aksesnya aman tanpa harus membuka file riil di disk.

```c
static int x_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, "/tujuan.txt") == 0) {
        return 0; // Izinkan langsung untuk file virtual
    }

    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    int fd = open(fullpath, fi->flags);
    if (fd == -1) return -errno;

    close(fd);
    return 0;
}
```

### 4. Rekonstruksi Data Rekursif (`x_read`)
Ini adalah otak dari program ini. Ketika kamu membaca file `/tujuan.txt` (misalnya menggunakan perintah `cat`), FUSE akan mengeksekusi logika berikut:
1.  **Looping File Log**: Program melakukan perulangan dari indeks `1` sampai `7` untuk membuka file `1.txt` hingga `7.txt` di folder sumber secara berurutan.
2.  **Parsing Baris**: Menggunakan fungsi `fgets()`, program membaca baris demi baris dari setiap file.
3.  **Penyaringan Kata Kunci**: Menggunakan `strncmp(line, "KOORD:", 6)`, program mencari baris yang diawali dengan string `KOORD:`.
4.  **Ekstraksi String**: Jika ditemukan, fragmen setelah kata `KOORD:` akan diambil menggunakan `sscanf(line, "KOORD: %[^\n]", frag)`.
5.  **Penggabungan (Concatenation)**: Fragmen tersebut digabungkan ke sebuah buffer utama bernama `final` menggunakan `strcat()`.
6.  **Pengiriman ke User**: Hasil gabungan string tersebut disalin ke buffer pengguna menggunakan `memcpy()` berdasarkan `offset` dan `size` yang diminta.

```c
static int x_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (strcmp(path, "/tujuan.txt") == 0)
    {
        char final[4096] = ""; // Buffer untuk menampung hasil gabungan
        char filepath[1024];
        char line[256];
        char frag[256];

        // Loop untuk membaca file 1.txt sampai 7.txt secara berurutan
        for (int i = 1; i <= 7; i++)
        {
            sprintf(filepath, "%s/%d.txt", source_dir, i);
            FILE *fp = fopen(filepath, "r");
            if (fp == NULL) continue;

            // Baca baris demi baris
            while (fgets(line, sizeof(line), fp))
            {
                // Jika baris diawali dengan kata "KOORD:"
                if (strncmp(line, "KOORD:", 6) == 0)
                {
                    // Ambil string setelah kata "KOORD: "
                    sscanf(line, "KOORD: %[^\n]", frag);
                    // Gabungkan ke dalam string utama (final)
                    strcat(final, frag);
                }
            }
            fclose(fp);
        }

        strcat(final, "\n"); // Tambahkan jembatan baris baru di akhir data
        size_t len = strlen(final);

        // Kirim hasil gabungan ke buffer sistem operasi sesuai offset dan size
        if (offset < len) {
            if (offset + size > len) size = len - offset;
            memcpy(buf, final + offset, size);
        } else {
            size = 0;
        }
        return size;
    }

    // LOGIKA PEMBACAAN FILE ASLI
    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    int fd = open(fullpath, O_RDONLY);
    if (fd == -1) return -errno;

    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;

    close(fd);
    return res;
}
```
---

##  Output

Berdasarkan isi file `1.txt` sampai `7.txt` yang ada pada sistem, berikut adalah fragmen data yang berhasil dikumpulkan secara otomatis oleh fungsi `x_read`:

* **Hari 1 (`1.txt`)**: `-7.957`
* **Hari 2 (`2.txt`)**: `382728`
* **Hari 3 (`3.txt`)**: `443728, `
* **Hari 4 (`4.txt`)**: `112.469`
* **Hari 5 (`5.txt`)**: `8688227961, `
* **Hari 6 (`6.txt`)**: `23:`
* **Hari 7 (`7.txt`)**: `59 WIB`

Saat membaca `tujuan.txt`, output akhir yang dienkapsulasi dan ditampilkan secara utuh adalah:
```text
-7.957382728443728, 112.4698688227961, 23:59 WIB
```

## Screenshoot
### ls amba_files


<img width="533" height="46" alt="Screenshot 2026-05-17 at 23 35 35" src="https://github.com/user-attachments/assets/0076b8ad-cebf-428c-926f-cc2ed03e11ff" />

### ls mnt


<img width="567" height="72" alt="Screenshot 2026-05-17 at 23 34 22" src="https://github.com/user-attachments/assets/812c91aa-468e-405e-812e-0669fadab4ad" />

### cat mnt/1.txt & cat amba_files/1.txt


<img width="782" height="284" alt="Screenshot 2026-05-17 at 23 38 19" src="https://github.com/user-attachments/assets/a0ab5bd3-6fef-4741-b380-08ea73191ae9" />

 ### for i in 1 2 3 4 5 6 7; do diff mnt/$i.txt amba_files/$i.txt && echo "$i.txt OK"done

 
<img width="783" height="142" alt="Screenshot 2026-05-17 at 23 40 54" src="https://github.com/user-attachments/assets/3cef4268-d3b0-497d-9051-0f9ccfcc725d" />

### cat mnt/tujuan.txt


<img width="742" height="42" alt="Screenshot 2026-05-17 at 23 41 41" src="https://github.com/user-attachments/assets/005b5c1e-8712-4aa5-884a-2bcc92f43d7b" />



