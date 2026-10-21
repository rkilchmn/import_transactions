<?php
/**
 * The documents that are attached to the transactions of a bulk import.
 *
 * The import form takes the documents either as individual uploads or as one zip file that carries
 * the documents of the import. The zip file belongs to the import when it has the name of the csv
 * file with the .zip extension: the csv 'supplier_invoices.csv' is accompanied by the zip file
 * 'supplier_invoices.zip'.
 *
 * The name of a document in the csv column 'attachment' is looked up in the uploads first and in
 * the zip afterwards, so that both ways of delivering the documents can be mixed.
 *
 * The documents are written to the attachment store of the company - the directory the attachment
 * page of the application uses - and are registered with add_attachment(), so that they are shown
 * with the invoice exactly like the documents that were attached by hand.
 */

include_once(__DIR__ . "/../../../admin/db/attachments_db.inc");

/**
 * The file types the application accepts as an attachment, the same list the attachment page of
 * the application uses.
 */
function import_attachment_extensions(): array
{
	return array('JPG', 'PNG', 'GIF', 'PDF', 'DOC', 'ODT');
}

/**
 * The mime types of the documents that are read from a zip file, a browser does not tell the type
 * of a file that was not uploaded.
 */
function import_attachment_mime_types(): array
{
	return array(
		'JPG' => 'image/jpeg',
		'PNG' => 'image/png',
		'GIF' => 'image/gif',
		'PDF' => 'application/pdf',
		'DOC' => 'application/msword',
		'ODT' => 'application/vnd.oasis.opendocument.text',
	);
}

/**
 * The name of a file without its directory and without its extension.
 */
function import_attachment_base_name(string $name): string
{
	$name = basename($name);
	$dot = strrpos($name, '.');

	return $dot === false ? $name : substr($name, 0, $dot);
}

/**
 * The documents of one import run.
 */
final class import_attachment_library
{
	/** @var array<string, array> the uploaded documents, by file name */
	private array $uploaded = [];
	/** @var array<string, array> the documents of the zip file, by base name */
	private array $zipped = [];
	/** @var ZipArchive|null the zip file that carries the documents */
	private ?ZipArchive $zip = null;
	/** @var string the name of the uploaded zip file */
	private string $zip_name = '';
	/** @var array<string> the documents that were written to the attachment store */
	private array $written = [];
	/** @var array<string> the names of the documents that were attached */
	private array $used = [];
	/** @var array<string> the problems that were found in the uploads */
	private array $errors = [];
	/** @var bool true for a trial run, nothing is written to the attachment store */
	private bool $trial = false;
	/** @var int the number of documents that were attached */
	private int $attached = 0;
	/** @var int the number of documents that would be attached in a trial run */
	private int $pending = 0;
	/** @var bool true when the documents that were written are to be kept */
	private bool $keep = false;

	/**
	 * @param string $csv_name the name of the uploaded csv file, the zip file that belongs to the
	 * import has the same name with the .zip extension
	 */
	public function __construct(string $csv_name = '')
	{
		$this->read_uploads($csv_name);
	}

	/**
	 * Removes the documents that were written to the attachment store when the run was not
	 * committed, so that no document is left behind that the database does not know about.
	 */
	public function __destruct()
	{
		if (!$this->keep) {
			$this->discard_written();
		}
	}

	/**
	 * Reads the uploaded files, the zip file that belongs to the import is opened.
	 */
	private function read_uploads(string $csv_name): void
	{
		if (!isset($_FILES['att']) || !is_array($_FILES['att'])) {
			return;
		}
		$files = $_FILES['att'];
		if (!isset($files['name']) || !is_array($files['name'])) {
			// one file was posted, the browser did not send an array
			$files = array(
				'name' => array($files['name'] ?? ''),
				'tmp_name' => array($files['tmp_name'] ?? ''),
				'size' => array($files['size'] ?? 0),
				'type' => array($files['type'] ?? ''),
				'error' => array($files['error'] ?? UPLOAD_ERR_NO_FILE),
			);
		}
		$csv_base = import_attachment_base_name($csv_name);
		foreach ($files['name'] as $index => $posted_name) {
			$name = basename((string)$posted_name);
			$tmp_name = (string)($files['tmp_name'][$index] ?? '');
			$error = (int)($files['error'][$index] ?? UPLOAD_ERR_NO_FILE);
			if ($name === '' || $error === UPLOAD_ERR_NO_FILE) {
				continue;
			}
			if ($error !== UPLOAD_ERR_OK) {
				$this->errors[] = sprintf(_("The file %s could not be uploaded (error %d)"), $name, $error);
				continue;
			}
			if ($tmp_name === '' || !is_uploaded_file($tmp_name)) {
				$this->errors[] = sprintf(_("The file %s was not received"), $name);
				continue;
			}
			// the zip file that carries the documents of the import has the name of the csv file
			if (strtolower($name) === $csv_base . '.zip') {
				$this->open_zip($tmp_name, $name);
				continue;
			}
			$this->uploaded[$name] = array(
				'source' => 'upload',
				'tmp_name' => $tmp_name,
				'size' => (int)($files['size'][$index] ?? 0),
				'type' => (string)($files['type'][$index] ?? ''),
			);
		}
	}

	/**
	 * Opens the zip file that carries the documents of the import and lists its entries.
	 */
	private function open_zip(string $tmp_name, string $name): void
	{
		if (!class_exists('ZipArchive')) {
			$this->errors[] = _("The zip file cannot be read, the zip extension is not available in php.");
			return;
		}
		if ($this->zip !== null) {
			$this->errors[] = sprintf(_("More than one file named %s was uploaded"), $name);
			return;
		}
		$zip = new ZipArchive();
		if ($zip->open($tmp_name) !== true) {
			$this->errors[] = sprintf(_("The file %s is not a readable zip file"), $name);
			return;
		}
		$this->zip = $zip;
		$this->zip_name = $name;
		for ($i = 0; $i < $zip->numFiles; $i++) {
			$stat = $zip->statIndex($i);
			if ($stat === false) {
				continue;
			}
			$entry = (string)$stat['name'];
			if (substr($entry, -1) === '/') {
				continue; // a directory
			}
			$base = basename($entry);
			if ($base === '' || $base === '.' || $base === '..') {
				continue;
			}
			if (isset($this->zipped[$base])) {
				$this->errors[] = sprintf(_("The zip file %s holds more than one file named %s"), $name, $base);
				continue;
			}
			$this->zipped[$base] = array(
				'source' => 'zip',
				'entry' => $entry,
				'size' => (int)$stat['size'],
				'type' => '',
			);
		}
	}

	/**
	 * The problems that were found in the uploads.
	 *
	 * @return array<int, string>
	 */
	public function errors(): array
	{
		return $this->errors;
	}

	/**
	 * True when no document was uploaded at all.
	 */
	public function is_empty(): bool
	{
		return $this->uploaded === [] && $this->zipped === [];
	}

	/**
	 * True when a zip file carries the documents of the import.
	 */
	public function has_zip(): bool
	{
		return $this->zip !== null;
	}

	/**
	 * The name of the zip file that carries the documents of the import.
	 */
	public function zip_name(): string
	{
		return $this->zip_name;
	}

	/**
	 * A trial run does not write anything to the attachment store.
	 */
	public function set_trial(bool $trial): void
	{
		$this->trial = $trial;
	}

	/**
	 * Looks a document up by name, the uploads first and the zip afterwards.
	 *
	 * @return array|null the document, null when it was not uploaded
	 */
	public function resolve(string $name): ?array
	{
		if ($name === '') {
			return null;
		}
		if (isset($this->uploaded[$name])) {
			return $this->uploaded[$name];
		}
		if (isset($this->zipped[$name])) {
			return $this->zipped[$name];
		}
		// a document that was written with another case of its name, the windows file systems
		// do not distinguish the case of a name
		foreach (array($this->uploaded, $this->zipped) as $index) {
			foreach ($index as $key => $document) {
				if (strcasecmp($key, $name) === 0) {
					return $document;
				}
			}
		}
		return null;
	}

	/**
	 * Checks a document against the rules the attachment page of the application uses.
	 *
	 * @return string the problem with the document, an empty string when it is acceptable
	 */
	public function check(string $name, array $document): string
	{
		$extension = strtoupper(pathinfo($name, PATHINFO_EXTENSION));
		if (!in_array($extension, import_attachment_extensions(), true)) {
			return _("Only graphics, pdf, doc and odt files are supported.");
		}
		if (strlen($name) > 60) {
			return _("The file name exceeds the maximum of 60 chars.");
		}
		if ((int)($document['size'] ?? 0) <= 0) {
			return _("The file is empty.");
		}
		return '';
	}

	/**
	 * Claims a document for one transaction, a document belongs to one transaction only.
	 */
	public function claim(string $name): bool
	{
		if (isset($this->used[$name])) {
			return false;
		}
		$this->used[$name] = true;

		return true;
	}

	/**
	 * Writes a document to the attachment store of the company and registers it with the
	 * transaction it belongs to.
	 *
	 * @param array $document the document as returned by resolve()
	 */
	public function store(array $document, string $name, int $type, int $trans_no, string $description): bool
	{
		if ($this->trial) {
			$this->pending++;
			return true;
		}
		$dir = import_attachment_dir();
		if ($dir === '') {
			display_error(_("The attachment directory of the company could not be created."));
			return false;
		}
		$unique_name = random_id();
		$destination = $dir . '/' . $unique_name;
		$extension = strtoupper(pathinfo($name, PATHINFO_EXTENSION));
		if (($document['source'] ?? '') === 'zip') {
			if ($this->zip === null) {
				return false;
			}
			$stream = $this->zip->getStream((string)$document['entry']);
			if ($stream === false) {
				display_error(sprintf(_("The document %s could not be read from %s"), $name, $this->zip_name));
				return false;
			}
			$out = fopen($destination, 'wb');
			if ($out === false) {
				fclose($stream);
				display_error(sprintf(_("The document %s could not be written"), $name));
				return false;
			}
			stream_copy_to_stream($stream, $out);
			fclose($stream);
			fclose($out);
			$size = (int)filesize($destination);
			$type_mime = import_attachment_mime_types()[$extension ?? ''] ?? '';
		} else {
			if (!move_uploaded_file((string)$document['tmp_name'], $destination)) {
				display_error(sprintf(_("The document %s could not be written"), $name));
				return false;
			}
			$size = (int)$document['size'];
			$type_mime = (string)$document['type'];
		}
		$this->written[] = $destination;
		add_attachment($type, $trans_no, $description, $name, $unique_name, $size, $type_mime);
		$this->attached++;
		return true;
	}

	/**
	 * The number of documents that were attached in this run.
	 */
	public function attached_count(): int
	{
		return $this->attached;
	}

	/**
	 * The number of documents that would be attached in a trial run.
	 */
	public function pending_count(): int
	{
		return $this->pending;
	}

	/**
	 * Keeps the documents that were written, the run was committed.
	 */
	public function keep_written(): void
	{
		$this->keep = true;
	}

	/**
	 * Removes the documents that were written to the attachment store.
	 */
	public function discard_written(): void
	{
		foreach ($this->written as $path) {
			if (file_exists($path)) {
				@unlink($path);
			}
		}
		$this->written = [];
	}
}

/**
 * The directory the documents of the company are stored in, it is created when it does not exist.
 */
function import_attachment_dir(): string
{
	$dir = company_path() . '/attachments';
	if (!file_exists($dir)) {
		if (!@mkdir($dir, 0777, true)) {
			return '';
		}
		// keep the directory out of the reach of a browser
		$index_file = $dir . '/index.php';
		if (!file_exists($index_file)) {
			$fp = @fopen($index_file, 'w');
			if ($fp !== false) {
				fwrite($fp, "<?php\nheader(\"Location: ../index.php\");\n");
				fclose($fp);
			}
		}
	}
	return is_dir($dir) ? $dir : '';
}

/**
 * The column of the csv file that holds the name of the document of a transaction.
 *
 * @return int|null the column, null when the type of the import has no documents
 */
function import_attachment_column(int $type): ?int
{
	switch ($type) {
		case ST_SUPPINVOICE:
			return 13;
		case ST_SALESORDER:
		case ST_SALESINVOICE:
			return 25;
		default:
			return null;
	}
}

/**
 * Checks the documents of the csv file before anything is written to the database, so that a
 * missing document is reported before the invoices are created and not after.
 *
 * @param resource $fp the open csv file
 * @return int the number of problems that were found
 */
function import_attachment_check_csv($fp, int $type, string $sep, import_attachment_library $library): int
{
	$column = import_attachment_column($type);
	if ($column === null) {
		return 0;
	}
	$errors = 0;
	$line = 0;
	rewind($fp);
	while (is_array($data = fgetcsv($fp, 4096, $sep, '"', '\\'))) {
		$line++;
		if ($line == 1) {
			continue; // the header
		}
		$name = trim(import_cell($data, $column));
		if ($name === '') {
			continue;
		}
		$document = $library->resolve($name);
		if ($document === null) {
			display_error(sprintf(_("The document %s of line %d was not uploaded"), $name, $line));
			$errors++;
			continue;
		}
		$problem = $library->check($name, $document);
		if ($problem !== '') {
			display_error(sprintf(_("Error on line %d: %s (%s)"), $line, $problem, $name));
			$errors++;
		}
	}
	rewind($fp);
	return $errors;
}

/**
 * Attaches the document of a transaction to the transaction.
 *
 * @return bool false when the document could not be attached
 */
function import_attachment_attach(import_attachment_library $library, string $name, int $type, int $trans_no, string $description): bool
{
	if ($name === '') {
		return true;
	}
	$document = $library->resolve($name);
	if ($document === null) {
		display_error(sprintf(_("The document %s was not uploaded"), $name));
		return false;
	}
	$problem = $library->check($name, $document);
	if ($problem !== '') {
		display_error(sprintf(_("%s (%s)"), $problem, $name));
		return false;
	}
	if (!$library->claim($name)) {
		display_error(sprintf(_("The document %s is used by more than one transaction"), $name));
		return false;
	}
	return $library->store($document, $name, $type, $trans_no, $description);
}
