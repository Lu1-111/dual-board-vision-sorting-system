---
name: vmware-uisrc
description: Use vmrun to inspect or operate the user's single Ubuntu VMware Player VM, including guest commands and file transfers. Applies only to the fixed uisrc VM.
---

# VMware uisrc VM

Control only `D:\uisrc_ubuntu\uisrc-ubuntu18x64\uisrc-ubuntu64x.vmx` with `D:\VMware\vmrun.exe -T player`. Never substitute another VMX or use this skill to operate another VM.

Use [scripts/invoke-vmrun.ps1](scripts/invoke-vmrun.ps1) for VM operations. It fixes the target VM and authenticates to its Ubuntu guest as `uisrc` using the local, Windows-encrypted `credential.xml`. Do not print the saved password or put it in a generated script or report. The credential file is specific to this Windows user and computer and should not be shared.

The user authorizes both reading and writing in this VM. Do not impose a read-only workflow. Carry out requested edits, builds, transfers, and VM actions with the normal task-specific care. Guest commands run as `uisrc`; if the guest OS requires elevated privileges, check available `sudo` access rather than assuming the account is root.

## Calls

- For guest shell work, call `pwsh -File <skill>/scripts/invoke-vmrun.ps1 -GuestScript '<bash script>'`. This mode returns stdout, stderr, and the guest exit code, and removes its temporary files.
- For a VM-specific `vmrun` command, call `pwsh -File <skill>/scripts/invoke-vmrun.ps1 -VmrunCommand <command> -Arguments <command arguments>`. The helper always inserts the fixed VMX after the command. Examples: `checkToolsState`, `getGuestIPAddress`, `CopyFileFromGuestToHost`, `CopyFileFromHostToGuest`, `start`, `stop`, and `snapshot`.
- Use a PowerShell here-string or a script file's contents for complex guest shell scripts. Keep paths and quoting explicit. Check the exit code and verify the requested result in the guest.

The helper rejects commands that address the host broadly or create another VM. If it cannot connect, report the exact error; do not switch to a different VM or silently fall back to SSH or raw virtual-disk access.
