<?php
/**
 * Direct supplier invoice import.
 *
 * Creates the same documents as Purchasing -> Purchase Orders -> Direct Purchase Invoice Entry
 * (purchasing/po_entry_items.php?NewInvoice=Yes): a purchase order of the type ST_SUPPINVOICE
 * together with the goods received and the supplier invoice.
 *
 * Note: purchasing/includes/ui/po_ui.inc is deliberately not included, it would redeclare
 * copy_to_cart() from import_sales_order_entry.inc. purchasing/includes/ui/invoice_ui.inc is
 * only included when the gl item display functions are not loaded yet, they would be
 * redeclared as well, so the due date is calculated here when it is not available.
 */

include_once(__DIR__ . "/../../../purchasing/includes/po_class.inc");
include_once(__DIR__ . "/../../../purchasing/includes/purchasing_db.inc");
if (!function_exists('get_duedate_from_terms') && !function_exists('display_gl_items')) {
	include_once(__DIR__ . "/../../../purchasing/includes/ui/invoice_ui.inc");
}

/**
 * The supplier invoice that is currently being collected.
 */
final class import_supplier_state
{
	// the purchase order the invoice lines are added to
	public ?purch_order $cart = null;
	// the document reference of the invoice being collected
	public string $reference = '';
	// the supplier of the invoice being collected
	public string $supplier_id = '';
	// the supplier's own invoice number of the invoice being collected
	public string $supp_ref = '';
	// the name of the document that is attached to the invoice
	public string $attachment = '';
	// invoices written
	public int $invoices = 0;
}

/**
 * Checks one csv line, returns an error message or an empty string when the line is acceptable.
 */
function import_supplier_invoice_check(import_supplier_line $row, int $line): string
{
	if ($row->supplier_id === '' || !is_numeric($row->supplier_id)) {
		return _("The supplier id is missing or is not numeric");
	}
	if (!get_supplier($row->supplier_id)) {
		return _("The supplier does not exist in the database");
	}
	if ($row->supp_ref === '') {
		return _("The supplier's invoice reference is missing");
	}
	if (!is_date($row->date)) {
		return _("The invoice date is invalid");
	}
	if (!is_date_in_fiscalyear($row->date)) {
		return _("The invoice date is out of fiscal year or is closed for further data entry");
	}
	if ($row->due_date !== '' && !is_date($row->due_date)) {
		return _("The due date is invalid");
	}
	if ($row->item_code === '') {
		return _("The item code is missing");
	}
	if (!get_item($row->item_code)) {
		return _("The item does not exist in the database");
	}
	if (!is_numeric($row->quantity) || (float)$row->quantity <= 0) {
		return _("The quantity must be a number greater than zero");
	}
	if (!is_numeric($row->price) || (float)$row->price < 0) {
		return _("The price must be a number not less than zero");
	}
	if ($row->location !== '' && !get_item_location($row->location)) {
		return _("The stock location does not exist in the database");
	}
	return '';
}

/**
 * The first stock location of the company, used when the csv does not name one.
 */
function import_supplier_invoice_default_location(): string
{
	$locations = get_item_locations(false);
	$row = db_fetch($locations);

	return $row === false ? '' : (string)$row['loc_code'];
}

/**
 * The due date of the invoice: the date of the csv, or the one the payment terms of the
 * supplier give when the csv does not name one.
 */
function import_supplier_invoice_due_date(purch_order $cart, import_supplier_line $row): void
{
	if ($row->due_date !== '') {
		$cart->due_date = $row->due_date;
		return;
	}
	if (function_exists('get_duedate_from_terms')) {
		get_duedate_from_terms($cart);
		return;
	}
	// the same calculation as get_duedate_from_terms() in purchasing/includes/ui/invoice_ui.inc
	if (!empty($cart->terms['day_in_following_month'])) {
		$cart->due_date = add_days(end_month($cart->orig_order_date), $cart->terms['day_in_following_month']);
	} else {
		$cart->due_date = add_days($cart->orig_order_date, $cart->terms['days_before_due']);
	}
}

/**
 * Creates the purchase order the supplier invoice is built from.
 */
function import_supplier_invoice_cart(import_supplier_line $row): purch_order
{
	// the cart constructor picks the supplier up from $_POST, the supplier is set below,
	// so it is kept out of the way here
	unset($_POST['supplier_id']);

	$cart = new purch_order();
	$cart->trans_type = ST_SUPPINVOICE;
	$cart->order_no = 0;

	get_supplier_details_to_order($cart, $row->supplier_id);

	$cart->orig_order_date = $row->date;
	$cart->due_date = $cart->orig_order_date;
	import_supplier_invoice_due_date($cart, $row);
	$cart->supp_ref = $row->supp_ref;
	$cart->Comments = $row->comments;
	$cart->Location = $row->location !== '' ? $row->location : import_supplier_invoice_default_location();
	$cart->delivery_address = '';
	if ($row->dimension_id !== '') {
		$cart->dimension = $row->dimension_id;
	}
	if ($row->dimension2_id !== '') {
		$cart->dimension2 = $row->dimension2_id;
	}

	return $cart;
}

/**
 * Adds one line to the supplier invoice.
 */
function import_supplier_invoice_add_line(purch_order $cart, import_supplier_line $row): void
{
	$item = get_item($row->item_code);
	$description = $row->item_description !== '' ? $row->item_description : (string)$item['description'];
	$line_no = count($cart->line_items);

	$cart->add_to_order($line_no, $row->item_code, (float)$row->quantity, $description,
		(float)$row->price, '', $cart->orig_order_date, 0, 0, 0, 0);
}

/**
 * Writes the supplier invoice that is being collected and starts a new one.
 */
function import_supplier_invoice_write(import_supplier_state $state, import_attachment_library $library): bool
{
	if ($state->cart === null) {
		return true;
	}
	$state->cart->reprice_order();
	$trans_no = add_direct_supp_trans($state->cart);
	if (!$trans_no) {
		display_error(_("The supplier invoice could not be created"));
		return false;
	}
	$state->invoices++;
	$attachment = $state->attachment;
	$reference = $state->reference;
	$supp_ref = $state->supp_ref;
	$state->cart = null;
	$state->reference = '';
	$state->attachment = '';

	if ($attachment !== '') {
		$description = sprintf(_("Supplier invoice %s (%s)"), $reference, $supp_ref);
		if (!import_attachment_attach($library, $attachment, ST_SUPPINVOICE, (int)$trans_no, $description)) {
			return false;
		}
	}

	return true;
}

/**
 * Processes one csv line of a supplier invoice. The invoice being collected is held in $state,
 * it is written as soon as the document reference changes. Returns true when the line had an error.
 */
function import_supplier_invoice_line(import_supplier_line $row, int $line, import_supplier_state $state, import_attachment_library $library): bool
{
	/** @var references $Refs */
	global $Refs;

	$error = import_supplier_invoice_check($row, $line);
	if ($error !== '') {
		display_error(sprintf(_("Error on line %d: %s"), $line, $error));
		return true;
	}

	// the invoice number of the supplier has to be unique for the supplier
	if (is_reference_already_there($row->supplier_id, $row->supp_ref)) {
		display_error(sprintf(_("Error on line %d: This invoice number has already been entered. It cannot be entered again. (%s)"), $line, $row->supp_ref));
		return true;
	}

	$location = $row->location !== '' ? $row->location : import_supplier_invoice_default_location();
	$context = array(
		'date' => $row->date,
		'location' => $location,
		'supplier' => $row->supplier_id,
	);

	// resolve the document reference: '+' continues the current invoice,
	// a blank cell lets get_next() assign the next free reference
	$reference = $row->reference;
	if ($reference === '+') {
		$reference = $state->reference;
	}

	// a new invoice starts when the supplier or its invoice number changes
	$new_invoice = ($state->cart !== null)
		&& (($state->supplier_id !== $row->supplier_id) || ($state->supp_ref !== $row->supp_ref));

	// write the previous invoice before resolving the new auto-reference, so that
	// get_next() sees the just-written reference and the sequence advances
	if ($new_invoice && !import_supplier_invoice_write($state, $library)) {
		return true;
	}

	if ($reference === '') {
		$reference = $Refs->get_next(ST_SUPPINVOICE, null, $context);
	}

	if (!$Refs->is_valid($reference, ST_SUPPINVOICE, $context)) {
		display_error(sprintf(_("Error on line %d: Reference %s must match valid pattern in Setup->Transaction References"), $line, $reference));
		return true;
	}
	if (!$Refs->is_new_reference($reference, ST_SUPPINVOICE)) {
		display_error(sprintf(_("Error on line %d: Reference %s has already been used"), $line, $reference));
		return true;
	}

	if ($new_invoice || $state->cart === null) {
		$state->cart = import_supplier_invoice_cart($row);
		$state->reference = $reference;
		$state->supplier_id = $row->supplier_id;
		$state->supp_ref = $row->supp_ref;
	}
	$state->cart->reference = $reference;

	import_supplier_invoice_add_line($state->cart, $row);

	// the document of the invoice is named in the attachment column of one of its lines
	if ($state->attachment === '' && $row->attachment !== '') {
		$state->attachment = trim($row->attachment);
	}

	return false;
}
