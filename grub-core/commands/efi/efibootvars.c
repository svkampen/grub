/* efibootvars.c - Modify EFI boot variables. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2012  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/command.h>
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/charset.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const grub_efi_guid_t efi_var_guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;

typedef struct efi_load_option {
  grub_efi_uint32_t attributes;
  grub_efi_uint16_t filepathlistlength;
  grub_efi_char16_t description[];
  /* there's more fields here but it's not currently used in this module. */
} efi_load_option_t;

/*
 * Verifies that all entries in a list of boot entries, in hexadecimal form,
 * exist and are accessible to GRUB.
 *
 * Returns the first invalid boot entry, if it exists.
 * Returns NULL, otherwise.
 */
static char *
validate_bootorder_entries (int num_entries, char **entries)
{
  char boot_entry_var[] = "Boot0000";

  efi_load_option_t *boot_entry;
  grub_size_t boot_entry_size;

  for (int i = 0; i < num_entries; ++i)
    {
      grub_strncpy (boot_entry_var + 4, entries[i], 4);

      boot_entry = grub_efi_get_variable (boot_entry_var, &efi_var_guid,
					  &boot_entry_size);
      if (! boot_entry)
	return entries[i];

      grub_free (boot_entry);
    }

  return NULL;
}

/*
 * Get/set the EFI BootNext variable.
 * To set, provide the boot entry in hexadecimal form (e.g. 001F).
 */
static grub_err_t
grub_cmd_bootnext (grub_command_t cmd __attribute__ ((unused)),
		   int argc,
		   char **args)
{
  grub_err_t status;
  grub_efi_uint16_t new_bootnext;
  grub_efi_uint16_t *cur_bootnext;
  grub_size_t sz;

  if (argc == 0)
    {
      cur_bootnext = grub_efi_get_variable ("BootNext", &efi_var_guid, &sz);

      if (cur_bootnext)
	grub_printf ("BootNext: %04x\n", *cur_bootnext);
      else if (grub_errno == GRUB_ERR_NONE)
	grub_printf ("BootNext: not set.\n");

      grub_free (cur_bootnext);
      return grub_errno;
    }


  if (validate_bootorder_entries (1, args))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "%s: boot entry inaccessible",
		       args[0]);

  new_bootnext = grub_strtoul (args[0], NULL, 16);

  if (grub_errno != GRUB_ERR_NONE)
    return grub_errno;

  status = grub_efi_set_variable ("BootNext", &efi_var_guid, &new_bootnext,
				  sizeof (new_bootnext));
  return status;
}

/*
 * Prints a boot entry along with its description.
 *
 * boot_entry should be of the form BootXXXX, where the X's are hex digits.
 */
static grub_err_t
print_single_boot_entry (char *boot_entry)
{
  efi_load_option_t *opt = NULL;
  char *description_utf8 = NULL;
  grub_size_t load_opt_size;
  grub_efi_char16_t *desc_offset;

  opt = grub_efi_get_variable (boot_entry, &efi_var_guid, &load_opt_size);

  if (! opt)
    goto end;

  for (desc_offset = opt->description; *desc_offset; desc_offset++);
  description_utf8 = grub_malloc (4 * (desc_offset - opt->description) + 1);

  if (! description_utf8)
    goto end;

  *grub_utf16_to_utf8 ((grub_uint8_t *) description_utf8, opt->description,
		       desc_offset - opt->description) = 0;

  grub_printf ("%s: %s\n", boot_entry, description_utf8);

end:
  grub_free (opt);
  grub_free (description_utf8);
  return grub_errno;
}

/* Validate boot order format: XXXX XXXX... where all X are hex digits. */
static int
validate_bootorder_fmt (int entries, char **order)
{
  char c;

  for (int i = 0; i < entries; ++i)
    {
      char *entry = order[i];

      while ((c = *entry++) != '\0')
	{
	  if (! grub_isxdigit (c))
	    return 0;
	}

      if (entry - order[i] > 5)
	return 0;
    }

  return 1;
}

/*
 * View or edit the EFI BootOrder variable.
 *
 * When a list of boot numbers is passed, this list is set as the
 * boot order. If nothing is passed, the current boot order is printed.
 */
static grub_err_t
grub_cmd_bootorder (grub_command_t cmd __attribute__ ((unused)),
		    int argc,
		    char **args __attribute__((unused)))
{
  grub_size_t num_entries, order_size;
  grub_efi_uint16_t *order;
  grub_efi_status_t status;
  char *invalid_entry;

  if (argc == 0)
    {
      order = grub_efi_get_variable ("BootOrder", &efi_var_guid, &num_entries);

      if (! order)
	return grub_errno;

      num_entries /= sizeof (grub_efi_uint16_t);

      grub_printf ("Boot order: ");

      for (unsigned i = 0; i < num_entries - 1; ++i)
	{
	  grub_printf ("%04x, ", order[i]);
	}

      grub_printf ("%04x.\n", order[num_entries - 1]);
      grub_free (order);

      return GRUB_ERR_NONE;
    }

  if (!validate_bootorder_fmt (argc, args))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid boot order format");

  invalid_entry = validate_bootorder_entries (argc, args);

  if (invalid_entry)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "%s: boot entry inaccessible",
		       invalid_entry);

  order_size = argc * sizeof (grub_efi_uint16_t);
  order      = grub_malloc (order_size);

  if (! order)
    return grub_errno;

  for (int i = 0; i < argc; ++i)
    {
      order[i] = grub_strtoul (args[i], NULL, 16);
    }

  status = grub_efi_set_variable ("BootOrder", &efi_var_guid, order, order_size);

  grub_free (order);
  return status;
}


/* Prints a list of boot entries, along with their descriptions. */
static grub_err_t
grub_cmd_bootentries (grub_command_t cmd __attribute__ ((unused)),
		      int argc __attribute__ ((unused)),
		      char **args __attribute__ ((unused)))
{
  grub_efi_uintn_t size = 0;
  char *tmp, *name = NULL;
  grub_efi_guid_t vendor_guid;

  grub_printf ("Boot entries:\n");

  /*
   * Go through all EFI variables, check if they are of the form BootXXXX,
   * if so, print them.
   */
  while ((tmp = grub_efi_get_next_variable_name (&size, name, &vendor_guid)))
    {
      name = tmp;

      if (grub_strncmp ("Boot", name, 4) == 0)
	{
	  for (int i = 4; i < 8; ++i)
	  {
	    if (!grub_isxdigit (name[i])) goto next_variable;
	    /*
	     * This assignment could be narrowing, but in that case,
	     * isxdigit would have failed, so we never reach the assignment.
	     */
	  }

	  if (name[8] != 0)
	    goto next_variable;

	  if (print_single_boot_entry (name) != GRUB_ERR_NONE)
	    goto end;
	}

next_variable:
      continue;
    }

end:
  grub_free (name);
  return grub_errno;
}

static grub_command_t cmd_bootnext, cmd_bootorder, cmd_bootentries;

GRUB_MOD_INIT (efibootorder)
{
  cmd_bootnext = grub_register_command ("bootnext", grub_cmd_bootnext, "[bootnum]",
	  N_("View or edit the UEFI BootNext variable.\n\n"
	     "By default, prints the value of BootNext. To edit, provide a "
	     "boot entry in hexadecimal form (e.g. 001F)."));

  cmd_bootorder = grub_register_command ("bootorder", grub_cmd_bootorder, "[bootnum]...",
	  N_("View or edit the UEFI boot order.\n\n"
	     "By default, prints the current boot order. To edit, provide "
	     "a space-separated list of boot entries in hexadecimal form "
	     "(e.g. 001F 0020 000A)."));

  cmd_bootentries = grub_register_command ("bootentries", grub_cmd_bootentries, NULL,
	  N_("Print UEFI boot entries with their description."));
}

GRUB_MOD_FINI (efibootorder)
{
  grub_unregister_command (cmd_bootentries);
  grub_unregister_command (cmd_bootorder);
  grub_unregister_command (cmd_bootnext);
}
