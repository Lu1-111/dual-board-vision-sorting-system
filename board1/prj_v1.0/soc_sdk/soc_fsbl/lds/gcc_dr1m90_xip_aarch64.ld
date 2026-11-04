/*
 * Copyright (c) 2023, Anlogic Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

__ROM_BASE = 0x70400000;
__ROM_SIZE = 0x100000;

MEMORY
{
    flash  : ORIGIN = __ROM_BASE, LENGTH = __ROM_SIZE
    ocm    : ORIGIN = 0x61000000, LENGTH = 0x3F000
    npuram : ORIGIN = 0x63e00000, LENGTH = 256K
}

REGION_ALIAS("START_VMA", flash)
REGION_ALIAS("START_LMA", flash)
REGION_ALIAS("CODE_VMA", ocm)
REGION_ALIAS("CODE_LMA", flash)
REGION_ALIAS("DATA_VMA", ocm)
REGION_ALIAS("DATA_LMA", flash)


SECTIONS
{
    __stack_size = DEFINED(__stack_size) ? __stack_size : 8K;

    .start : ALIGN(4096)
    {
        *al_aarch64_start.S.o(.text)
        *al_mpu.S.o(.text)
        *al_dr1m90.S.o(.text)
        *al_top.S.o(.text)
        *al_chip.S.o(.text)
        *al_aarch64_pagetable.S.o(.text)
    }  > START_VMA AT>START_LMA

    /*
     * [_ilm ~ _eilm]: data in flash, will be copied to ocm from _ilm_lma
    */
    .ilalign         :
    {
        . = ALIGN(4);
        PROVIDE( _ilm_lma = LOADADDR(.text) );
    }

    .ialign         :
    {
        PROVIDE( _ilm = ADDR(.text) );
    }

    . = ALIGN(4096);

    .text : ALIGN(4096)
    {
        PROVIDE(_text_start  = .);
        *(.text)
        *(.text.*)
        PROVIDE(_text_end  = .);
    }  > CODE_VMA AT>CODE_LMA

    .rodata : ALIGN(16)
    {
        PROVIDE(_rodata_start = .);
        *(.rodata)
        . = ALIGN(4);
        *(.rdata)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        /* section information for initial. */
        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;
        /* section information for finsh shell */
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
        . = ALIGN(4);
        __rt_utest_tc_tab_start = .;
        KEEP(*(UtestTcTab))
        __rt_utest_tc_tab_end = .;
        *(.gnu.linkonce.r.*)
        /* section information for usbh class */
        . = ALIGN(4);
        __usbh_class_info_start__ = .;
        KEEP(*(.usbh_class_info))
        __usbh_class_info_end__ = .;
        PROVIDE(_rodata_end   = .);
    }  > CODE_VMA AT>CODE_LMA

    .ctors          :
    {
        /* gcc uses crtbegin.o to find the start of
        the constructors, so we make sure it is
        first.  Because this is a wildcard, it
        doesn't matter if the user does not
        actually link against crtbegin.o; the
        linker won't look for a file to match a
        wildcard.  The wildcard also means that it
        doesn't matter which directory crtbegin.o
        is in.  */
        KEEP (*crtbegin.o(.ctors))
        KEEP (*crtbegin?.o(.ctors))
        /* We don't want to include the .ctor section from
        the crtend.o file until after the sorted ctors.
        The .ctor section from the crtend file contains the
        end of ctors marker and it must be last */
        KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*(.ctors))

        PROVIDE(__ctors_start__ = .);
        /* new GCC version uses .init_array */
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__ctors_end__ = .);
    } >CODE_VMA AT>CODE_LMA

    .eh_frame : {
       *(.eh_frame)
    } >CODE_VMA AT>CODE_LMA

    .eh_framehdr : {
       __eh_framehdr_start = .;
       *(.eh_framehdr)
       __eh_framehdr_end = .;
    } >CODE_VMA AT>CODE_LMA

    .gcc_except_table : {
       *(.gcc_except_table)
    } >CODE_VMA AT>CODE_LMA

    .pagetable : ALIGN(4096)
    {
        . = ALIGN(4096);
        PROVIDE(_pagetable_start = .);
        *(.page_table)
        PROVIDE(_pagetable_end   = .);
    }  > CODE_VMA AT>CODE_LMA

    PROVIDE (__etext = .);
    PROVIDE (_etext = .);
    PROVIDE (etext = .);
    PROVIDE( _eilm = . );

    /*
     * [_data ~ _edata]: data in flash, will be copied to ocm from _data_lma
    */

    .lalign : ALIGN(32)
    {
        . = ALIGN(32);
        PROVIDE( _data_lma = LOADADDR(.data) );
    } >DATA_VMA

    .dalign : ALIGN(32)
    {
        . = ALIGN(32);
        PROVIDE( _data = ADDR(.data));
    } >DATA_VMA

    PROVIDE(_data_start = .);
    .data : ALIGN(32)
    {
        . = ALIGN(32);
        *(.data*)
        _no_cache_section_start = (. & 0x200000);
        _no_cache_section_end   = (. & 0x200000);
    }  > DATA_VMA AT>DATA_LMA

    .cli_section : ALIGN(8)
    {
        _cli_section_start = .;
        KEEP(*(cli_section))
        _cli_section_end   = .;
    }  > DATA_VMA AT>DATA_LMA

    .unity_section : ALIGN(8)
    {
        _unity_test_start = .;
        KEEP(*(unity_section))
        _unity_test_end   = .;
    }  > DATA_VMA AT>DATA_LMA

    .gbe_phy_section : ALIGN(8)
    {
        _gbe_phy_section_start = .;
        KEEP(*(gbe_phy_section))
        _gbe_phy_section_end   = .;
    }  > DATA_VMA AT>DATA_LMA

    PROVIDE( _edata = . );
    PROVIDE( edata = . );

    /*
     * bss segment
    */
    .bss : ALIGN(0x10)
    {
        _bss_start = .;
        *(.bss*)
        _bss_end = .;
    }  > DATA_VMA

    _end = .;
    _heap_start = .;

    PROVIDE(_heap_end = ORIGIN(DATA_VMA) + LENGTH(DATA_VMA) - __stack_size);

    .stack ORIGIN(DATA_VMA) + LENGTH(DATA_VMA) - __stack_size (NOLOAD) :
    {
        PROVIDE(stack_start = .);
        . = ALIGN(16);
        . = __stack_size;
        PROVIDE(stack_top = .);
        PROVIDE(_sp = .);
        . = ALIGN(16);
    } > DATA_VMA
}
