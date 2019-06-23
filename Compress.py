from PIL import Image

def get_compressed(filename):
    # Convert to gray and then to 1 bit using custom threshold
    im = Image.open(filename).convert(mode="L").point(lambda i: i > 127 and 255)

    imdata = list(im.getdata())

    imbit = []
    b_count = 0
    c8 = 0
    for c in imdata[:]:
        c8 = c8 << 1
        if c != 0:
            c8 = c8 | 1
        b_count += 1
        if b_count == 8:
            b_count = 0
            imbit.append(c8)
            c8 = 0


    # ENCODE
    im_comp = []
    c_m1 = imbit[0]
    runlength = 0
    # run-length encode only bytes 0 and 255
    # use 0x55 as escape sequence for 0 and 0xaa for 255
    # afterwards the run-length is encoded into 1 or 2 bytes
    # for run-lengths of 1..127 1 byte is used, for run-lengths
    # above the highest bit is set to 1 and the MSB is added
    # to the second byte.
    # run length of 0 is used to replace escape character
    for c in imbit[1:]:
        if runlength == 0:
            if c_m1 in [0, 255]:
                if c_m1 == c:
                    runlength = 2
                else:
                    im_comp.append(c_m1)
            else:
                # encode directly
                im_comp.append(c_m1)
                if c_m1 in [0x55, 0xaa]:
                    im_comp.append(0)
        else:
            if c_m1 == c:
                runlength += 1
            else:
                if runlength == 2:
                    im_comp.append(c_m1)
                    im_comp.append(c_m1)
                else:
                    if c_m1 == 0:
                        im_comp.append(0x55)
                    else:
                        im_comp.append(0xaa)
                    if runlength <= 127:
                        im_comp.append(runlength)
                    else:
                        im_comp.append((runlength & 0x7f) | 128)
                        im_comp.append(runlength >> 7)
                runlength = 0
        c_m1 = c

    if runlength == 0:
        im_comp.append(c_m1)
    else:
        if runlength == 2:
            im_comp.append(c_m1)
            im_comp.append(c_m1)
        else:
            if c_m1 == 0:
                im_comp.append(0x55)
            else:
                im_comp.append(0xaa)
            if runlength <= 127:
                im_comp.append(runlength)
            else:
                im_comp.append((runlength & 0x7f) | 128)
                im_comp.append(runlength >> 7)
        runlength = 0

    # DECODE
    im_decomp = []
    runlength = -1
    c_to_dup = -1
    for c in im_comp[:]:
        if c_to_dup == -1:
            if c in [0x55, 0xaa]:
                c_to_dup = c
            else:
                im_decomp.append(c)
        else:
            if runlength == -1:
                if c == 0:
                    im_decomp.append(c_to_dup)
                    c_to_dup = -1
                elif (c & 0x80) == 0:
                    if c_to_dup == 0x55:
                        im_decomp.extend([0] * c)
                    else:
                        im_decomp.extend([255] * c)
                    c_to_dup = -1
                else:
                    runlength = c & 0x7f
            else:
                runlength = runlength | (c << 7)
                if c_to_dup == 0x55:
                    im_decomp.extend([0] * runlength)
                else:
                    im_decomp.extend([255] * runlength)
                c_to_dup = -1
                runlength = -1

    if len(imbit) != len(im_decomp):
        print("Decomp len fail!")
    else:
        for a, b in zip(imbit, im_decomp):
            if (b < 0) or (b > 255):
                print("Range to big")
            if a != b:
                print("Decomp fail")
                break
    return im_comp, imbit

sumlen = 0
movie = []
output_file = open("E:\\Proggen\\BA\\video.bin","wb")
output_file_uc = open("E:\\Proggen\\BA\\video.uc","wb")
# 6573 5470/2
for nr in range(1, int(6574)):
    fn = "E:\Proggen\BA\scene" + "{0:0>5}".format(nr) + ".png"
    comp_dat, uncomp = get_compressed(fn)
    output_file.write(bytearray(comp_dat))
    output_file_uc.write(bytearray(uncomp))
    # movie.extend(comp_dat)
    sumlen += len(comp_dat)

output_file.close()
output_file_uc.close()
#movie_dat = bytearray(movie)
#print(len(movie_dat))

print(sumlen)
print("Done")
#im.show()
