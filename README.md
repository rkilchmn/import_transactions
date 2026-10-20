import_transactions
===================

FrontAccounting module to import transactions from csv.

Formats supported:

* journal
* payment
* deposit
* adapted bank statement
* sales order / direct sales invoice
* **direct supplier invoice** (new in this fork)

## Provenance

Based on [rossaddison/import_transactions](https://github.com/rossaddison/import_transactions)
(version of 2020-05-03), by Ross Addison <frontaccounting@bbqq.co.uk> — GPL-2.0,
see [LICENSE](LICENSE).

This repository is **not** a fork of the upstream repository. It contains the
module version that is installed and tested in FrontAccounting 2.4.18 with
PHP 8.4, plus the changes listed below. The upstream project has since been
refactored (it now has `includes/import_rows.inc` and
`includes/import_transactions_ui.inc`); that refactoring is not included here.

## Changes on top of the 2020 upstream version

### PHP 8.4 compatibility

The module overrode the application error handling with
`error_reporting(E_ALL)` and `ini_set("display_errors", "on")`. Since
`E_ALL` (32767) is not equal to the `$cur_error_level` of FrontAccounting
(-1), the application wide `error_handler()` in `includes/errors.inc` was
silently disabled and every warning and deprecation raised during an import
was swallowed before it reached the message box. The override was removed so
that the module reports problems the same way the rest of the application
does.

With the error handling restored the following problems showed up and were
fixed:

* `fgetcsv()` was called without the `$escape` argument, which is deprecated
  since PHP 8.4 and changes its default in PHP 9.
* Four functions had an optional parameter in front of a required one, which
  is deprecated since PHP 8.4: `checkbox_row()`, `csv_demo_line()`,
  `journal_inclusive_tax()` and `can_process()`.
* `list()` assignments read undefined array keys when a csv row had fewer
  columns than expected. A new `import_csv_row()` helper pads or truncates
  the row to the number of expected columns.
* Database lookups were dereferenced without checking the result, which
  produced "Trying to access array offset on false" warnings:
  `get_dimension_id_from_dimref()`, `customer_name()`, `supplier_name()`,
  `determinetax()`, `get_person_by_name()`, `check_tax_appropriate()`,
  `get_sales_type_id()` and `check_customer_supplier()`.
* The cart class created dynamic properties, which is deprecated since
  PHP 8.2: `deliver_to` was written to an undeclared `delivery_to` property,
  and `bo_policy` and `payment_info` were missing.
* `prep_amount`, `payment_terms` and `prepaid` had no default value, so an
  empty value was written into numeric database columns. MySQL rejected this
  with *"Incorrect double value: '' for column `prep_amount`"*, which aborted
  the whole import.
* The same happened for `ship_via`, `freight_cost`, `payment`, `dimension_id`
  and `dimension2_id` when those csv columns were left empty. They are now
  normalised to numbers before they reach the database.
* `check_import_item_data()` divided by the exchange rate without checking
  that a rate was available, which caused a division by zero error.
* A `+` in the reference column of a sales document was never resolved, so
  every line of a document started a new document and the line was rejected
  as "you must enter a reference". The reference of the previous line is now
  used for the continuation lines.
* The reference of a sales or supplier document was registered twice, once
  with the line counter and once by the application with the document
  number. The registration by the module was dropped for those document
  types.

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

## Installation

Copy the module into `core/modules/import_transactions` of your
FrontAccounting installation and install it under *Setup -> Modules*. The
menu entry *General Ledger -> Import &Transactions* is added by the module.

## Original upstream description

> Front Accounting version 2.4: Import transactions from csv in journal
> format, payment format, deposit format, or adapted bank statement format.
> Features: 1. Trial check before importing. 2. Tabular display of journal
> entries, journals displayed as debits and credits using Front Accounting's
> 'items cart' class. 3. Importing of bank statements with additional columns
> for spreadsheet adjusted associated transaction account codes, customer, or
> supplier id's. 4. Additional Tax type column for VAT registered companies
> for vat inclusive income or expenses. 5. Inclusion of transactions
> automatically in an audit trail. 6. Display notifications identifying how
> tables within the database are being affected for a more transparent
> display to interested programmers. 7. Additional lookup tools for looking up
> customer id's, supplier id's, company setup information eg. fiscal year,
> and other tools that users will find useful for inclusion in their
> spreadsheet prior to conversion to csv. 8. Suitable validation checks for
> customers, suppliers, tax codes, references. 9. Example csv files are
> provided for each format. These can be used to test the package, and as
> templates for your own use. 10. Import multi-line or single-line sales
> orders or invoices.
