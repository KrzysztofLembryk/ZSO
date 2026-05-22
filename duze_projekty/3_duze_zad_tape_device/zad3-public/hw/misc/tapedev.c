// NOTE: Please do not look at source of the device before submitting the 
// final version of the project.
// If you have any questions, or doubts if something works correctly, please
// ask on Slack.

/*
 * ZSO Tape device. Based on pci-testdev.c and edu.c
 *
 * Copyright 2026 Franciszek Stachura
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "trace.h"

#define TYPE_PCI_TAPEDEV "tapedev"
struct PCIint;

typedef hwaddr hwaabu ;
typedef dma_addr_t dma_aabu_t ;


OBJECT_DECLARE_SIMPLE_TYPE(PCIint, PCI_TAPEDEV)
# 44 "hw/misc/tapedev.c"
typedef struct {
    PCIint *d;
    int id;
    uint32_t aadf;
    uint32_t aaaa;
    dma_aabu_t aaab;
    uint32_t aaac;
    uint32_t aaad;
    uint32_t aaae;
    int aadg;
    uint32_t aaaf;
    uint32_t *aaag;
    int64_t aaah;
    uint64_t aaai;
    uint64_t aaaj;
    uint16_t aaak;
    uint16_t aaal;
    uint16_t aaam;
    uint16_t aaan;
    uint32_t aaao;
    int aadb;
    char *aada;
    uint64_t *aaap;
} aacc;

static void free_aade(aacc *s);
static void aabm(aacc *s, uint64_t cmd_val);

static uint32_t aaar(aacc* s)
{
    uint32_t aaadize = (32 * (1 << (s->aaae)) * 8192);

    assert(s->aaaa != 0);

    return (s->aaaa-1)*aaadize+s->aaaf;
}

static void aaas(aacc* s, uint32_t offset)
{
    assert(s->aaaa != 0);

    s->aaaf += offset;
    s->aaag[s->aaaa-1] = s->aaaf;
}

static void aaat(aacc* s)
{
    assert(s->aaaa != 0);

    s->aaaf = 0;
    s->aaag[s->aaaa-1] = s->aaaf;
}

static void aaau(aacc* s)
{
    assert(s->aaaa != 0);

    s->aaaa = 0;
    s->aaaf = 0;
}

static void aaaw(aacc* s, uint32_t tape)
{
    assert(tape != 0 && tape <= s->aaad);
    assert(s->aaaa == 0);

    s->aaaa = s->aaai;
    s->aaaf = s->aaag[s->aaaa-1];
}

static bool aaax(aacc* s)
{
    uint16_t aaam = 0;

    while (aaam < s->aaam) {
        if (s->aaan >= (4096/8)) {
            return false;
        }

        uint64_t aabs = s->aaap[s->aaan];
        uint32_t aabr = ((aabs) & 0xffffffff);
        uint64_t aabt = ((aabs) >> 32);

        if (aabt == 0 || aabr == 0) {
            return false;
        }

        aabr--;
        if (aabr == 0) {
            s->aaan++;
            s->aaao = 0;
        } else {
            s->aaap[s->aaan] = (((((uint64_t)((aabt) & 0xffffffff)) << 32) | ((uint64_t)(aabr) & 0xffffffff)));
            s->aaao++;
        }

        aaam++;
    }

    return true;
}

static void aaaz(aacc* s)
{
    s->aaai = 0;
    s->aaaj = 0;
    s->aaak = 0;
    s->aaal = 0;
    s->aaam = 0;
    s->aaan = 0;
    s->aaao = 0;
}

static void aaba(aacc* s)
{
    uint64_t aadc = s->aaap[s->aaan];
    uint32_t aadd = ((aadc) & 0xffffffff);
    uint32_t aabu = ((aadc) >> 32);

    aadd--;

    if (aadd == 0) {
        s->aaap[s->aaan] = (((((uint64_t)((aabu) & 0xffffffff)) << 32) | ((uint64_t)(aadd) & 0xffffffff)));
        s->aaan++;
        s->aaao = 0;
    } else {
        s->aaap[s->aaan] = (((((uint64_t)((aabu) & 0xffffffff)) << 32) | ((uint64_t)(aadd) & 0xffffffff)));
        s->aaao++;
    }
}

static dma_aabu_t aabb(aacc* s)
{
    dma_aabu_t result;
    uint64_t aadc = s->aaap[s->aaan];
    uint32_t aaddize = (1 << (9+(s->aaac)));

    if (((aadc) >> 32) == 0 || ((aadc) & 0xffffffff) == 0) {
        result = 0;
    } else {
        result = (((aadc) >> 32) << 9) +
                aaddize*s->aaao;
    }
# 235 "hw/misc/tapedev.c"
    return result;
}
# 246 "hw/misc/tapedev.c"
struct PCIint {
    PCIDevice dev;
    MemoryRegion mmio;

    QEMUTimer aabp;

    uint32_t aadf;
    uint32_t irq_aadf;
    uint32_t irq_mask;

    int aadg;
    int64_t aaah;


    uint32_t aades_no;
    aacc *aades;


    char *data_dir;
    bool fast_mode;

    uint32_t prop_len_aaad;
    uint32_t *prop_aaad;
    uint32_t prop_len_aadh;
    uint8_t *prop_aadh;
};

static void raise_irq(PCIint *d, uint32_t irq)
{
    d->irq_aadf |= irq;

    if (!!(irq & ~d->irq_mask)) {
        if (msi_enabled(&d->dev)) {
            msi_notify(&d->dev, 0);

        } else {
            pci_set_irq(&d->dev, 1);

        }
    }
}

static void clear_irq(PCIint *d, uint32_t val)
{
    uint32_t irq_aadf = d->irq_aadf;
    d->irq_aadf &= ~val;
    uint32_t new_irq_aadf = irq_aadf & ~val;



    if (new_irq_aadf == irq_aadf)
        return;

    if (!msi_enabled(&d->dev)) {

        pci_set_irq(&d->dev, 0);
    } else {

    }
}

static void aabc(aacc *s, int st, uint32_t aadf)
{
    s->aadg = st;
    s->aadf = aadf;
    uint32_t new_irq_mask;

    if (aadf & 0x80) {
        new_irq_mask = (1 << (24+s->id));
    } else {
        new_irq_mask = (1 << (16+s->id));
    }

    aaaz(s);
    raise_irq(s->d, new_irq_mask);
}

static bool aabf(PCIint *d);

static void aabd(aacc *s, int st, int64_t time)
{
    s->aadg = st;
    s->aadf = 0x01;

    if (s->d->fast_mode) {
        s->aaah = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) - 1;
    } else {
        s->aaah = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + time == 0 ? 0 : (rand() % time);
    }
}

static void aabe(aacc *s, uint32_t offset)
{
    if (s->d->fast_mode) {
        s->aaah = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) - 1;
    } else {
        s->aaah = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + offset == 0 ? 0 : (rand() % offset);
    }
}


static uint64_t aabg(PCIint *d, hwaabu aabu)
{
    int aadg = d->aadg;

    if (aadg == 0 && aabu >= 0x14) {
        return 0;
    }

    switch (aabu) {
    case 0x00:
        return (aadg != 0 && aadg != 1);
    case 0x04:
        return d->aadf;
    case 0x08:
        return d->irq_aadf;
    case 0x0c:
        return d->irq_mask;
    case 0x10:
        return 0;
    case 0x14:
        return d->aades_no;
    }

    return 0;
}

static uint64_t aabj(PCIint *d, aacc *s, uint64_t aade_aabu)
{
    uint32_t val = 0;

    switch (aade_aabu) {
    case 0x00:
        val = (s->aadf != 1);
        break;
    case 0x04:
        val = s->aadf;
        break;
    case 0x08:
        val = (s->aaab >> 9);
        break;
    case 0x0C:
        val = s->aaaa;
        break;
    case 0x10:
        val = s->aaad;
        break;
    case 0x14:
        val = s->aaae;
        break;
    case 0x18:
        val = s->aaac;
        break;
    }

    return val;
}

static uint64_t aabl(void *opaque, hwaabu aabu, unsigned size)
{
    PCIint *d = opaque;
    uint64_t val;
    int aadg = d->aadg;

    if (size != 4) {

        val = 0;
        goto ret;
    }

    if (aabu <= 0xfc) {
        val = aabg(d, aabu);
        goto ret;
    }

    if (aadg == 0 || aadg == 1) {

        val = 0;
        goto ret;
    }

    uint64_t aade = ((((aabu) & 0xff00) >> 8)-1);
    uint64_t aade_aabu = ((aabu) & 0xff);

    if (aade >= d->aades_no) {

        val = 0;
        goto ret;
    }



    val = aabj(d, &d->aades[aade], aade_aabu);

ret:


    return val;
}

static void aabh(PCIint *d, hwaabu aabu, uint64_t val)
{
    int aadg = d->aadg;

    switch (aabu) {
    case 0x00:
        if (val > 0 && aadg == 0) {

            d->aadg = 1;
        } else if (val == 0 && (aadg == 2 || aadg == 4)) {

            d->aadg = 3;
        }
        break;
    case 0x04:
        d->aadf = val;
        break;
    case 0x08:
        return;
    case 0x0c:
        d->irq_mask = val;
        break;
    case 0x10:
        clear_irq(d, val);
        break;
    case 0x14:
        return;
    }
}

static void aabi(PCIint *d, aacc *s, uint64_t aade_aabu, uint64_t val)
{
    if (s->d->aadg != 2) {

        return;
    }

    switch (aade_aabu) {
    case 0x00:
        aabm(s, val);
        break;
    case 0x04:
        s->aadf = val;
        break;
    case 0x08:
        s->aaab = (val << 9);
        break;
    case 0x0C:
        break;
    case 0x10:
        break;
    case 0x14:
        break;
    case 0x18:
        if (s->aadg == 1 && val <= 4)
            s->aaac = val;

        break;
    }
}

static void aabk(void *opaque, hwaabu aabu, uint64_t val,
                           unsigned size)
{
    PCIint *d = opaque;



    if (size != 4) {

        return;
    }

    if (aabu <= 0xfc) {

        aabh(d, aabu, val);
        return;
    }

    if (d->aadg == 0) {

        val = 0;
        return;
    }

    uint64_t aade = ((((aabu) & 0xff00) >> 8)-1);
    uint64_t aade_aabu = ((aabu) & 0xff);

    if (aade >= d->aades_no) {

        return;
    }


    aabi(d, &d->aades[aade], aade_aabu, val);
}

static const MemoryRegionOps tapedev_mmio_ops = {
    .read = aabl,
    .write = aabk,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};



static void aabm(aacc *s, uint64_t cmd_val)
{
    uint32_t aaddize = (1 << (9+(s->aaac)));
    uint32_t aaadize = (32 * (1 << (s->aaae)) * 8192);



    if (s->aadg != 1) {

        return;
    }

    if (s->aadg == 8 || s->d->aadg == 4) {

        aabc(s, 8, 0x83);
        return;
    }

    uint8_t cmd = cmd_val & 0xff;
    uint32_t arg = (cmd_val >> 8) & 0xffffff;
    uint32_t rw_aadd = (cmd_val >> 8) & ((1 << 15)-1);
    uint32_t rw_offset = (cmd_val >> (8+15)) & ((1 << 9)-1);

    switch (cmd) {
    case 0x00:
        if (s->aaaa != 0) {

            aabc(s, 1, 0x81);
        } else if (arg > s->aaad || arg <= 0) {

            aabc(s, 1, 0x84);
        } else {

            s->aaai = arg;

            aabd(s, 2, 100);
        }
        break;
    case 0x01:
        if (s->aaaa == 0) {

            aabc(s, 1, 0x82);
        } else {

            aabd(s, 3, 100);
        }
        break;
    case 0x02:
        if (s->aaaa == 0) {

            aabc(s, 1, 0x82);
        } else {


            aabd(s, 4, 100);
        }
        break;
    case 0x03:
        if (s->aaaa == 0) {

            aabc(s, 1, 0x82);
        } else if (arg >= (aaadize/aaddize)) {

            aabc(s, 1, 0x85);
        } else {


            s->aaaj = arg;
            aabd(s, 5, 100);
        }
        break;
    case 0x04:
        if (s->aaaa == 0) {

            aabc(s, 1, 0x82);
        } else {


            s->aaal = rw_aadd;
            s->aaam = rw_offset;
            s->aaak = 0;
            aabd(s, 6, 1);
        }
        break;
    case 0x05:
        if (s->aaaa == 0) {

            aabc(s, 1, 0x82);
        } else {


            s->aaal = rw_aadd;
            s->aaam = rw_offset;
            s->aaak = 0;
            aabd(s, 7, 1);
        }
        break;
    default:

        aabc(s, 1, 0x80);
    }

    if (s->d->fast_mode) {
        while (aabf(s->d)) ;
    }
}

static bool aabn(aacc* s)
{
    MemTxResult dma_err;


    s->aaan = 0;
    s->aaao = 0;

    dma_err = pci_dma_read(&s->d->dev, s->aaab, s->aaap, 4096);
    if (dma_err != MEMTX_OK) {

        aabc(s, 1, 0x89);
        return false;
    }

    if (!aaax(s)) {

        aabc(s, 1, 0x89);
        return false;
    }

    return true;
}

static void aabo(aacc* s, bool is_read)
{
    off_t lseek_err;
    ssize_t err;
    MemTxResult dma_err;
    uint32_t pos = aaar(s);
    uint32_t aaddize = (1 << (9+(s->aaac)));
    uint32_t aaadize = (32 * (1 << (s->aaae)) * 8192);
    int i = s->aaak;
    dma_aabu_t page;

    if (s->aaaa == 0) {

        aabc(s, 1, 0x88);
        return;
    }

    if (s->aaab == 0) {
        aabc(s, 1, 0x89);

        return;
    }

    if (s->aaaf >= aaadize || s->aaaf+aaddize > aaadize) {

        aabc(s, 1, 0x86);
        return;
    }

    if (s->aaal <= 0) {

        aabc(s, 1, 0x02);

        return;
    }


    if (i == 0 && !aabn(s)) {
        return;
    }

    page = aabb(s);
    if (page == 0) {
        aabc(s, 1, 0x89);
        return;
    }




    lseek_err = lseek(s->aadb, pos, SEEK_SET);
    if (lseek_err < 0) {

        aabc(s, 8, 0x88);
            return;
    }

    if (is_read) {
        err = read(s->aadb, s->aada, aaddize);
        if (err < 0 || err < aaddize) {

            aabc(s, 8, 0x88);
            return;
        }



        dma_err = pci_dma_write(&s->d->dev, page, s->aada, aaddize);

        if (dma_err != MEMTX_OK) {

            aabc(s, 8, 0x89);
            return;
        }
    } else {


        dma_err = pci_dma_read(&s->d->dev, page, s->aada, aaddize);

        if (dma_err != MEMTX_OK) {

            aabc(s, 8, 0x89);
            return;
        }

        err = write(s->aadb, s->aada, aaddize);
        if (err < 0 || err < aaddize) {

            aabc(s, 8, 0x88);
            return;
        }
    }

    s->aaak += 1;
    aaba(s);
    aaas(s, aaddize);

    s->aaal -= 1;
    if (s->aaal > 0) {
        aabe(s, 1);
    } else {
        aabc(s, 1, 0x02);
    }
}

static bool aaca(PCIint *d)
{
    int64_t clock = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    int dev_aadg = d->aadg;

    if (dev_aadg == 1 && clock >= d->aaah) {


        d->aadg = 2;
        d->aadf = 1;

        for (int i=0; i != d->aades_no; i++) {
            aacc *s = &d->aades[i];
            s->aadg = 1;
        }

        raise_irq(d, (1 << 0));
    } else if (d->aadg == 3) {
        bool stop_ok = true;
        for (int i=0; i != d->aades_no; i++) {
            aacc *s = &d->aades[i];
            if (!(s->aadg == 1 || s->aadg == 8)) {
                stop_ok = false;
                break;
            } else {
                s->aadg = 0;
            }
        }

        if (stop_ok) {
            d->aadg = 0;
            d->aadf = 0;

            for (int i=0; i != d->aades_no; i++) {
                aacc *s = &d->aades[i];
                aaaz(s);
            }

            return true;
        } else {
            for (int i=0; i != d->aades_no; i++) {
                aacc *s = &d->aades[i];
                if (s->aadg == 0) {
                    s->aadg = 1;
                }
            }
        }
    }

    return false;
}

static bool aabf(PCIint *d)
{
    int64_t clock = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    int dev_aadg = d->aadg;
    bool work_done = false;

    if (dev_aadg == 0) {

        return false;

    } else {
        if (aaca(d)) {
            return true;
        }
    }

    for (int i=0; i != d->aades_no; i++) {
        aacc *s = &d->aades[i];

        bool no_work =
            s->aadg == 0 ||
            s->aadg == 8 ||
            s->aadg == 1 ||
            (!s->d->fast_mode && clock < s->aaah);

        if (no_work) {
            continue;
        }



        work_done = true;

        switch (s->aadg) {
        case 1:

            break;
        case 2:
            aaaw(s, s->aaai);

            aabc(s, 1, 0x02);
            break;
        case 3:

            aaau(s);
            aabc(s, 1, 0x02);
            break;
        case 4:

            aaat(s);
            aabc(s, 1, 0x02);
            break;
        case 5:

            aaas(s, s->aaaj*(1 << (9+(s->aaac))));
            aabc(s, 1, 0x02);
            break;
        case 6:

            aabo(s, true);
            break;
        case 7:

            aabo(s, false);
            break;
        case 8:
            break;
        default:

        }
    }

    return work_done;
}

static void aabp(void *data)
{
    PCIint *d = data;

    if (d->fast_mode) {
        aabf(d);
        timer_mod(&d->aabp, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        return;
    }

    aabf(d);
    timer_mod(&d->aabp, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
}


static int open_tape_file(int dir, int i, uint32_t size, Error **errp)
{
    char name[16];
    char zero = 0;
    int err, aadb;
    off_t offset;

    snprintf(name, sizeof(name), "sect_%d", i);

    aadb = openat(dir, name, O_CREAT | O_RDWR, 0664);
    if (aadb < 0) {
        error_setg_errno(errp, errno, "Failed to open tape file %d", i);
        return aadb;
    }

    offset = lseek(aadb, 0, SEEK_END);
    if (offset < 0) {
        error_setg_errno(errp, errno, "Failed to seek in tape file %d", i);
        return -1;
    }

    if (offset == size) {
        return aadb;
    }

    err = lseek(aadb, size-1, SEEK_SET);
    if (err < 0) {
        error_setg_errno(errp, errno, "Failed to seek in tape file %d", i);
        return err;
    }

    err = write(aadb, (void*)&zero, 1);
    if (err != 1) {
        error_setg_errno(errp, errno, "Failed to write end zero into tape file %d", i);
        return err;
    }

    err = lseek(aadb, 0, SEEK_SET);
    if (err < 0) {
        error_setg_errno(errp, errno, "Failed to seek in tape file %d", i);
        return err;
    }

    return aadb;
}

static void tapedev_realize(PCIDevice *pci_dev, Error **errp)
{
    PCIint *d = PCI_TAPEDEV(pci_dev);
    uint8_t *pci_conf;
    int dir, err;

    if (d->prop_len_aaad != d->prop_len_aadh) {
        error_setg(errp, "tapes len %u != tape_types len %u",
                d->prop_len_aaad, d->prop_len_aadh);
        return;
    }

    int num_aades = d->prop_len_aaad;

    if (num_aades > 8 || num_aades <= 0) {
        error_setg(errp, "invalid number of sections %u", num_aades);
        return;
    }

    for (int i=0; i != num_aades; i++) {
        if (d->prop_aadh[i] > 4) {
            error_setg(errp, "invalid tape size %d", d->prop_aadh[i]);
            return;
        }

        if (d->prop_aaad[i] > 10000 || d->prop_aaad[i] <= 0) {
            error_setg(errp, "invalid number of tapes: !(0 < %d <= 10000)", d->prop_aaad[i]);
            return;
        }
    }



    if (d->data_dir == NULL) {
        error_setg(errp, "data_dir not specified");
        return;
    }

    err = mkdir(d->data_dir, 0775);
    if (err < 0 && errno != EEXIST) {
        error_setg(errp, "Failed to create tape data directory");
        return;
    }

    dir = open(d->data_dir, O_DIRECTORY | O_PATH);
    if (dir < 0) {
        error_setg(errp, "Failed to open tape data directory");
        return;
    }



    d->aadg = 0;
    d->aadf = 0;
    d->aades_no = num_aades;
    d->aades = (aacc*)g_malloc0(sizeof(aacc)*num_aades);

    for (int i=0; i != num_aades; i++) {
        aacc* s = &d->aades[i];

        s->d = d;
        s->id = i;


        s->aadf = 0;
        s->aaaa = 0;
        s->aaab = 0;
        s->aaac = 0;

        s->aaad = d->prop_aaad[i];
        s->aaae = d->prop_aadh[i];

        s->aadg = 1;
        s->aaaf = 0;
        s->aaag = g_malloc(sizeof(*s->aaag)*d->prop_aaad[i]);
        s->aaah = 0;

        aaaz(s);

        s->aadb = open_tape_file(dir, i, s->aaad*(32 * (1 << (s->aaae)) * 8192), errp);
        if (s->aadb < 0) {
            goto fail_tape;
        }

        s->aada = g_malloc0(8192);

        s->aaap = g_malloc0(4096 +sizeof(*s->aaap));
    }



    pci_conf = pci_dev->config;


    pci_config_set_interrupt_pin(pci_conf, 1);


    if (msi_init(pci_dev, 0, 1, true, false, errp))
        return;



    memory_region_init_io(&d->mmio, OBJECT(d), &tapedev_mmio_ops, d,
            "tapedev-mmio", 0x10000);

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    timer_init_ms(&d->aabp, QEMU_CLOCK_VIRTUAL, aabp, d);
    timer_mod(&d->aabp, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);

    return;

fail_tape:
    for (int i=0; i != num_aades; i++) {
        if (d->aades[i].aadb != 0) {
            free_aade(&d->aades[i]);
        }
    }

    close(dir);
}

static void tapedev_reset(DeviceState *dev)
{
    PCIint *d = PCI_TAPEDEV(dev);

    d->aadf = 0;
    d->irq_aadf = 0;
    d->irq_mask = 0;

    d->aadg = 0;
    d->aaah = 0;

    for (int i=0; i != d->aades_no; i++) {
        aacc* s = &d->aades[i];

        s->aadf = 0;
        s->aaaa = 0;
        s->aaab = 0;
        s->aaac = 0;

        s->aadg = 1;
        s->aaaf = 0;
        s->aaah = 0;

        aaaz(s);
    }
}

static void free_aade(aacc *s)
{
    close(s->aadb);
    free(s->aada);
    free(s->aaap);
    free(s->aaag);
}

static void tapedev_uninit(PCIDevice *pci_dev)
{
    PCIint *d = PCI_TAPEDEV(pci_dev);

    for (int i=0; i != d->aades_no; i++) {
        free_aade(&d->aades[i]);
    }
    g_free(d->aades);

    timer_del(&d->aabp);
    msi_uninit(pci_dev);
}

static const Property tapedev_properties[] = {
    DEFINE_PROP_STRING("data_dir", PCIint, data_dir),
    DEFINE_PROP_BOOL("fast_mode", PCIint, fast_mode, true),
    DEFINE_PROP_ARRAY("tapes", PCIint,
        prop_len_aaad, prop_aaad, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_ARRAY("tape_types", PCIint,
        prop_len_aadh, prop_aadh, qdev_prop_uint32, uint8_t),
};

static void tapedev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = tapedev_realize;
    k->exit = tapedev_uninit;
    k->vendor_id = 0x664e;
    k->device_id = 0x7ea;
    k->revision = 0;
    k->class_id = PCI_CLASS_STORAGE_OTHER;
    dc->desc = "ZSO 2025/2026 tapedev";
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    device_class_set_legacy_reset(dc, tapedev_reset);
    device_class_set_props(dc, tapedev_properties);
}

static const TypeInfo tapedev_info = {
    .name = "tapedev",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIint),
    .class_init = tapedev_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void tapedev_register_types(void)
{
    type_register_static(&tapedev_info);
}

type_init(tapedev_register_types)
