import_transactions
===================

Front Accounting version 2.4: Import transactions from csv in journal format, payment format, deposit format, or adapted bank statement format.  Features: 1. Trial check before importing. 2. Tabular display of journal entries, journals displayed as debits and credits using Front Accounting's 'items cart' class. 3. Importing of bank statements with additional columns for spreadsheet adjusted associated transaction account codes, customer, or supplier id's. 4. Additional Tax type column for VAT registered companies for vat inclusive income or expenses. 5. Inclusion of transactions automatically in an audit trail. 6. Display notifications identifying how tables within the database are being affected for a more transparent display to interested programmers. 7. Additional lookup tools for looking up customer id's, supplier id's, company setup information eg. fiscal year, and other tools that users will find useful for inclusion in their spreadsheet prior to conversion to csv. 8. Suitable validation checks for customers, suppliers, tax codes, references. 9. Example csv files are provided for each format. These can be used to test the package, and as templates for your own use. 10. Import multi-line or single-line sales orders or invoices. 11. Import direct supplier invoices.

## Provenance

This repository started as a copy of the module version of 2020-05-03 and was
then merged with [rossaddison/import_transactions](https://github.com/rossaddison/import_transactions)
(the upstream project, by Ross Addison <frontaccounting@bbqq.co.uk>), whose
2026 rewrite made the importer work on PHP 7.4 to 8.5 with FrontAccounting 2.4.

The upstream project is GPL-2.0, see [LICENSE](LICENSE). The `upstream` remote
of this repository points at it, so the two can be compared and upstream
changes can be pulled in:

    git remote -v
    git fetch upstream
    git log --oneline upstream/master

The upstream rewrite brought a lot with it: typed csv row objects
(`includes/import_rows.inc`), the FrontAccounting `Cart` class instead of a
local copy of it, the form moved into `includes/import_transactions_ui.inc`,
and a psalm level 1 clean bill of health.

## Changes on top of the upstream rewrite

### PHP 8.4 as the minimum version

Upstream requires PHP 8.5 (`composer.json`, `psalm.xml` and the psalm
workflow). This repository supports PHP 8.4, which is what FrontAccounting
2.4.18 ships with. The psalm workflow now runs on 8.4 and 8.5.

### Direct supplier invoice import

A new import type *Direct Supplier Invoice Processing* creates the same
documents as *Purchasing -> Purchase Orders -> Direct Purchase Invoice
Entry* (`purchasing/po_entry_items.php?NewInvoice=Yes`).

The csv file has the following columns:

| Column | Required | Notes |
| --- | --- | --- |
| supplier_id | yes | The supplier id, see the supplier lookup under *Display* |
| reference | no | The Front Accounting document reference, it has to match the pattern of *Setup -> Transaction References*. Leave it empty to get the next reference, use `+` for the further lines of the same invoice |
| supp_ref | yes | The invoice number of the supplier, it has to be unique for the supplier |
| date | yes | The invoice date, it has to be within the current fiscal year |
| due_date | no | The due date, taken from the payment terms of the supplier when empty |
| item_code | yes | The item code, it has to be a text column in the spreadsheet to keep leading zeros |
| item_description | no | The item description, taken from the item master when empty |
| quantity | yes | The quantity, a number greater than zero |
| price | yes | The price, a number not less than zero |
| location | no | The stock location, the first location of the company when empty |
| dimension_id | no | The dimension id |
| dimension2_id | no | The second dimension id |
| comments | no | The comments of the invoice |

An example file is provided as [templates/supplier_invoice.csv](templates/supplier_invoice.csv).

The implementation is in [includes/import_supplier_invoice.inc](includes/import_supplier_invoice.inc).
It reuses the purchase order classes of the application. Two files of the
application are deliberately not included, because they would redeclare
functions of this module: `purchasing/includes/ui/po_ui.inc` (redeclares
`copy_to_cart()`) and `purchasing/includes/ui/invoice_ui.inc` (redeclares
`display_gl_items()`).

### Further fixes

* A `+` in the reference column of a sales document now continues the
  document of the previous line. Upstream started a new document for every
  line and registered the `+` itself as the reference of that document.
* The reference of a sales or a supplier document is no longer registered
  twice: the module registered it with the line counter, the application
  registers it again with the number of the document.
* The details of a customer are no longer looked up when the customer does
  not exist, which made the application read an empty record.
* The help link of the deposit template points at `deposit_en_GB.csv` again.
  Upstream renamed the file to `deposit_en_GB.CSV`, which the link does not
  find on a case sensitive file system.

## Installation

Copy the module into `core/modules/import_transactions` of your
FrontAccounting installation and install it under *Setup -> Modules*. The
menu entry *General Ledger -> Import &Transactions* is added by the module.
