<?php
//module/import_transactions
//
//	Direct supplier invoice import.
//
//	Creates the same documents as
//	Purchasing -> Purchase Orders -> Direct Purchase Invoice Entry
//	(purchasing/po_entry_items.php?NewInvoice=Yes) from a csv file.
//
//	Note: purchasing/includes/ui/po_ui.inc is deliberately not included here,
//	it would redeclare copy_to_cart() from import_sales_order_entry.inc.
//	invoice_ui.inc can only be included when the gl item display functions are
//	not loaded yet, they would be redeclared as well.

include_once($path_to_root . "/purchasing/includes/po_class.inc");
include_once($path_to_root . "/purchasing/includes/purchasing_db.inc");
if (!function_exists('get_duedate_from_terms') && !function_exists('display_gl_items'))
	include_once($path_to_root . "/purchasing/includes/ui/invoice_ui.inc");

//	Number of columns expected in a supplier invoice csv row
define('IMPORT_SUPPINVOICE_COLS', 13);

//	Column layout of a supplier invoice csv row
//	 0 supplier_id		1 reference		 2 supp_ref		 3 date
//	 4 due_date		 5 item_code		 6 item_description 7 quantity
//	 8 price			 9 location		10 dimension_id	11 dimension2_id
//	12 comments
function import_supplier_invoice_row_data($data)
{
	$row = import_csv_row($data, IMPORT_SUPPINVOICE_COLS);

	return array(
		'supplier_id'		=> trim($row[0]),
		'reference'			=> trim($row[1]),
		'supp_ref'			=> trim($row[2]),
		'date'				=> trim($row[3]),
		'due_date'			=> trim($row[4]),
		'item_code'			=> trim($row[5]),
		'item_description'	=> trim($row[6]),
		'quantity'			=> trim($row[7]),
		'price'				=> trim($row[8]),
		'location'			=> trim($row[9]),
		'dimension_id'		=> trim($row[10]),
		'dimension2_id'		=> trim($row[11]),
		'comments'			=> trim($row[12]),
	);
}

//	Check a supplier invoice csv row, returns an error message or an empty
//	string when the row is acceptable
function import_supplier_invoice_check($row, $line)
{
	if ($row['supplier_id'] === '' || !is_numeric($row['supplier_id']))
		return _("The supplier id is missing or is not numeric");
	if (!get_supplier($row['supplier_id']))
		return _("The supplier does not exist in the database");
	if ($row['supp_ref'] === '')
		return _("The supplier's invoice reference is missing");
	if (!is_date($row['date']))
		return _("The invoice date is invalid");
	if (!is_date_in_fiscalyear($row['date']))
		return _("The invoice date is out of fiscal year or is closed for further data entry");
	if ($row['due_date'] !== '' && !is_date($row['due_date']))
		return _("The due date is invalid");
	if ($row['item_code'] === '')
		return _("The item code is missing");
	if (!get_item($row['item_code']))
		return _("The item does not exist in the database");
	if (!is_numeric($row['quantity']) || $row['quantity'] <= 0)
		return _("The quantity must be a number greater than zero");
	if (!is_numeric($row['price']) || $row['price'] < 0)
		return _("The price must be a number not less than zero");
	if ($row['location'] !== '' && !get_item_location($row['location']))
		return _("The stock location does not exist in the database");

	return '';
}

//	The first stock location of the company, used when the csv does not
//	specify one
function import_supplier_invoice_default_location()
{
	$locations = get_item_locations(false);
	$row = db_fetch($locations);

	return $row ? $row['loc_code'] : '';
}

//	Due date of the invoice, taken from the supplier payment terms unless the
//	csv supplies one
function import_supplier_invoice_due_date($cart, $row)
{
	if ($row['due_date'] !== '') {
		$cart->due_date = $row['due_date'];
		return;
	}
	if (function_exists('get_duedate_from_terms')) {
		get_duedate_from_terms($cart);
		return;
	}
	//	same calculation as get_duedate_from_terms() in purchasing/includes/ui/invoice_ui.inc
	if (!empty($cart->terms['day_in_following_month']))
		$cart->due_date = add_days(end_month($cart->orig_order_date), $cart->terms['day_in_following_month']);
	else
		$cart->due_date = add_days($cart->orig_order_date, $cart->terms['days_before_due']);
}

//	Create the purchase order cart the supplier invoice is built from
function import_supplier_invoice_cart($row)
{
	//	the cart constructor picks the supplier up from $_POST, the supplier
	//	is set below, so it is kept out of the way here
	unset($_POST['supplier_id']);

	$cart = new purch_order;
	$cart->trans_type = ST_SUPPINVOICE;
	$cart->order_no = 0;

	get_supplier_details_to_order($cart, $row['supplier_id']);

	$cart->orig_order_date = $row['date'];
	$cart->due_date = $cart->orig_order_date;
	import_supplier_invoice_due_date($cart, $row);
	$cart->supp_ref = $row['supp_ref'];
	$cart->Comments = $row['comments'];
	$cart->Location = $row['location'] !== '' ? $row['location'] : import_supplier_invoice_default_location();
	$cart->delivery_address = '';
	if ($row['dimension_id'] !== '')
		$cart->dimension = $row['dimension_id'];
	if ($row['dimension2_id'] !== '')
		$cart->dimension2 = $row['dimension2_id'];

	return $cart;
}

//	Add a line to the supplier invoice
function import_supplier_invoice_add_line($cart, $row)
{
	$item = get_item($row['item_code']);
	$description = $row['item_description'] !== '' ? $row['item_description'] : $item['description'];
	$line_no = count($cart->line_items);

	$cart->add_to_order($line_no, $row['item_code'], $row['quantity'], $description,
		$row['price'], '', $cart->orig_order_date, 0, 0, 0, 0);
}

//	Write the supplier invoice, the transaction is closed by the import itself,
//	so a trial run is rolled back with the rest of the import
function import_supplier_invoice_write(&$cart)
{
	$cart->reprice_order();

	return add_direct_supp_trans($cart);
}

//	Start a new supplier invoice import
function import_supplier_invoice_start()
{
	return array('cart' => null, 'reference' => '', 'invoices' => 0, 'trial' => false);
}

//	Process a supplier invoice csv row. The invoice currently being built is
//	held in $state, it is written as soon as the document reference changes.
//	Returns true when the row produced an error.
function import_supplier_invoice_row($data, $line, &$state)
{
	global $Refs;

	$row = import_supplier_invoice_row_data($data);

	$error = import_supplier_invoice_check($row, $line);
	if ($error !== '') {
		display_error(sprintf(_("Error on line %d: %s"), $line, $error));
		return true;
	}

	//	the supplier's own invoice number has to be unique for the supplier
	if (is_reference_already_there($row['supplier_id'], $row['supp_ref'])) {
		display_error(sprintf(_("Error on line %d: This invoice number has already been entered. It cannot be entered again. (%s)"), $line, $row['supp_ref']));
		return true;
	}

	$location = $row['location'] !== '' ? $row['location'] : import_supplier_invoice_default_location();
	$context = array(
		'date'		=> $row['date'],
		'location'	=> $location,
		'supplier'	=> $row['supplier_id'],
	);

	//	resolve the document reference
	$reference = $row['reference'];
	if ($reference === '')
		$reference = $Refs->get_next(ST_SUPPINVOICE, null, $context);
	elseif ($reference === '+')
		$reference = $state['reference'];

	if (!$Refs->is_valid($reference, ST_SUPPINVOICE, $context)) {
		display_error(sprintf(_("Error on line %d: Reference %s must match valid pattern in Setup->Transaction References"), $line, $reference));
		return true;
	}
	if (!$Refs->is_new_reference($reference, ST_SUPPINVOICE)) {
		display_error(sprintf(_("Error on line %d: Reference %s has already been used"), $line, $reference));
		return true;
	}

	//	the reference changed, so the previous invoice is complete
	if (($state['cart'] !== null) && ($reference !== $state['reference'])) {
		if (!import_supplier_invoice_finish($state))
			return true;
	}

	//	the reference is the same, but the supplier or the invoice may have changed
	if (($state['cart'] !== null) && ($reference === $state['reference'])) {
		if (($state['supplier_id'] !== $row['supplier_id']) || ($state['supp_ref'] !== $row['supp_ref'])) {
			display_error(sprintf(_("Error on line %d: The supplier and the supplier's invoice reference have to be the same for all lines of an invoice"), $line));
			return true;
		}
	} else {
		$state['cart'] = import_supplier_invoice_cart($row);
		$state['reference'] = $reference;
		$state['supplier_id'] = $row['supplier_id'];
		$state['supp_ref'] = $row['supp_ref'];
	}
	$state['cart']->reference = $reference;

	import_supplier_invoice_add_line($state['cart'], $row);

	return false;
}

//	Write the invoice which is currently being built
function import_supplier_invoice_finish(&$state)
{
	if ($state['cart'] === null)
		return true;

	$trans_no = import_supplier_invoice_write($state['cart']);
	if (!$trans_no) {
		display_error(_("The supplier invoice could not be created"));
		return false;
	}
	$state['invoices']++;
	$state['cart'] = null;
	$state['reference'] = '';

	return true;
}
