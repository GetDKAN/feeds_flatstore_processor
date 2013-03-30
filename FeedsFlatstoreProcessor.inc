<?php

/**
 * @file
 * Definition of FeedsDataProcessor.
 */

/**
 * Creates simple table records from feed items. Uses Data module.
 */
class FeedsFlatstoreProcessor extends FeedsProcessor {

  // We aren't creating or updating entities, just table records.
  // So these are empty.
  /**
   * Modifies entity type.
   */
  public function entityType() {}

  /**
   * Modifies new entity.
   */
  protected function newEntity(FeedsSource $source) {}

  /**
   * Modifies entity save.
   */
  protected function entitySave($entity) {}

  /**
   * Modifies entity deletion.
   */
  protected function entityDeleteMultiple($entity_ids) {}

  /**
   * Implementation of FeedsProcessor::process().
   */
  public function process(FeedsSource $source, FeedsParserResult $parser_result) {
    // Count number of created and updated nodes.
    $inserted  = $updated = 0;
    $expiry_time = $this->expiryTime();
    $this->feed_nid = $source->feed_nid;

    $table = $this->table($source, $parser_result);

    while ($data = $parser_result->shiftItem()) {
      foreach ($data as $label => $value) {
        $cleaned_data[data_safe_name($label)] = $value;
      }
      $cleaned_data['feed_entity_id'] = $this->feed_nid;
      if (!isset($data['timestamp'])) {
        $cleaned_data['timestamp'] = FEEDS_REQUEST_TIME;
      }

      // Only save if this item is not expired.
      if ($expiry_time != FEEDS_EXPIRE_NEVER && $data['timestamp'] < (FEEDS_REQUEST_TIME - $expiry_time)) {
        continue;
      }
      $id = $this->existingItemId($parser_result, $source);
      if ($id != FALSE || $this->config['update_existing']) {
        $data['id'] = $id;
        $this->handler($source)->update($cleaned_data, 'id');
      }
      else {
        $this->handler($source)->save($cleaned_data);
      }
    }

  }

  /**
   * Count items imported by this source.
   */
  public function itemCount(FeedsSource $source) {
    $table_name = $this->tableName($source);
    if (db_table_exists($table_name)) {
      return db_query("SELECT count(*) FROM {" . $table_name . "}")->fetchField();
    }
    else {
      return NULL;
    }
  }

  /**
   * Implementation of FeedsProcessor::clear().
   *
   * Delete all data records for feed_entity_id in this table.
   */
  public function clear(FeedsSource $source) {
    $table_name = $this->tableName($source);
    $result = db_truncate($table_name)->execute();
    if ($result) {
      drupal_set_message('All items have been deleted.');
    }
    else {
      drupal_set_message('There were no items to delete.');
    }
  }

  /**
   * Implement expire().
   */
  public function expire($time = NULL) {
    if ($time === NULL) {
      $time = $this->expiryTime();
    }
    if ($time == FEEDS_EXPIRE_NEVER) {
      return FEEDS_BATCH_COMPLETE;
    }
    $clause = array(
      'timestamp' => array(
        '<',
        FEEDS_REQUEST_TIME - $time,
      ),
    );
    $num = $this->handler($source)->delete($clause);
    drupal_set_message(format_plural($num, 'Expired @number record from @table.', 'Expired @number records from @table.', array('@number' => $num, '@table' => $this->tableName())));
    return FEEDS_BATCH_COMPLETE;
  }

  /**
   * Return expiry time.
   */
  public function expiryTime() {
    return $this->config['expire'];
  }

  /**
   * Set target element, bring element in a FeedsDataHandler format.
   */
  public function setTargetElement(FeedsSource $source, $target_item, $target_element, $value) {
    if (empty($value)) {
      return;
    }
    if (strpos($target_element, '.')) {

      list($table, $field) = explode('.', $target_element);

      $values = array();
      $value = is_array($value) ? $value : array($value);
      foreach ($value as $v) {
        // Create a record array.
        $values[] = array(
          $field => $v,
        );
      }
      if (is_array($target_item["#$table"])) {
        $target_item["#$table"] = array_merge($target_item["#$table"], $values);
      }
      else {
        $target_item["#$table"] = $values;
      }
    }
    else {
      if (is_array($target_item[$target_element]) && is_array($value)) {
        $target_item[$target_element] = array_merge($target_item[$target_element], $value);
      }
      else {
        $target_item[$target_element] = $value;
      }
    }
  }

  /**
   * Iterate through unique targets and try to load existing records.
   */
  protected function existingItemId(FeedsParserResult $parser_result, FeedsSource $source) {
    foreach ($this->uniqueTargets($source, $parser_result) as $target => $value) {
      if ($records = $this->handler($source)->load(array('feed_entity_id' => $source->feed_entity_id, $target => $value))) {
        return $records[0]['id'];
      }
    }
    return FALSE;
  }

  /**
   * Override parent::configDefaults().
   */
  public function configDefaults() {
    return array(
      'update_existing' => FEEDS_SKIP_EXISTING,
       // Don't expire items by default.
      'expire' => FEEDS_EXPIRE_NEVER,
      'mappings' => array(),
      'delete_with_source' => FALSE,
    );
  }

  /**
   * Override parent::configForm().
   */
  public function configForm(&$form_state) {
    $period = drupal_map_assoc(array(
        FEEDS_EXPIRE_NEVER, 3600, 10800, 21600, 43200, 86400, 259200, 604800, 604800 * 4, 604800 * 12, 604800 * 24, 31536000,
      ),
      'feeds_format_expire');
    $form['expire'] = array(
      '#type' => 'select',
      '#title' => t('Expire items'),
      '#options' => $period,
      '#description' => t("Select after how much time data records should be deleted. The timestamp target value will be used for determining the item's age, see Mapping settings."),
      '#default_value' => $this->config['expire'],
    );
    $form['update_existing'] = array(
      '#type' => 'checkbox',
      '#title' => t('Replace existing records'),
      '#description' => t('If an existing record is found for an imported record, replace it. Existing records will be determined using mappings that are a "unique target".'),
      '#default_value' => $this->config['update_existing'],
    );
    return $form;
  }

  /**
   * Reschedule if expiry time changes.
   */
  public function configFormSubmit(&$values) {
    if ($this->config['expire'] != $values['expire']) {
      feeds_reschedule($this->id);
    }
    parent::configFormSubmit($values);
  }

  /**
   * Return the data table name for this feed.
   */
  protected function tableName(FeedsSource $source) {
    return feeds_flatstore_processor_table_name($source->id, $source->feed_nid);
  }

  /**
   * Return the data table for this feed.
   *
   * @throws Exception $e
   *   Throws this exception if a table cannot be found and cannot be created.
   */
  protected function table(FeedsSource $source, $parser_result) {
    if ($table = feeds_flatstore_processor_table($source, $parser_result)) {
      return $table;
    }
    else {
      throw new Exception(t('Could not create data table.'));
    }
  }

  /**
   * Return a data handler for this table.
   *
   * Avoids a call to table() to not unnecessarily instantiate DataTable.
   */
  protected function handler(FeedsSource $source) {
    return DataHandler::instance($this->tableName($source), 'id');
  }


  /**
   * Finds appropriate schema data types for each row.
   */
  public function dataSchema($parsed_results) {
    return feeds_flatstore_processor_find_columns($parsed_results);
  }

  /**
   * Creates full schema from baseSchema and an array.
   *
   * @param array $data
   *   Array with keys for each desired row.
   */
  public function fullSchema($data) {
    $base = $this->baseSchema();
    $data_schema = data_build_schema($data);
    $base['fields'] = array_merge($data_schema['fields'], $base['fields']);
    return $base;
  }

  /**
   * Every Feeds data table must have these elements.
   */
  protected function baseSchema() {
    return array(
      'fields' => array(
        'id' => array(
          'type' => 'serial',
          'size' => 'normal',
          'unsigned' => TRUE,
          'not null' => TRUE,
        ),
        'timestamp' => array(
          'description' => 'The Unix timestamp for the data.',
          'type' => 'int',
          'unsigned' => TRUE,
          'not null' => FALSE,
        ),
      ),
      'indexes' => array(
        'id' => array('id'),
        'timestamp' => array('timestamp'),
      ),
      'primary key' => array(
        '0' => 'id',
      ),
    );
  }

  /**
   * Source form to delete.
   */
  public function sourceForm($source_config) {
    $form = array();
    $output = '';
    $form['#weight'] = -10;
    $nid = arg(1);
    // If we have a record for table settings.
    if (FALSE) {
      $config = ctools_export_load_object('feeds_importer', 'conditions', array('id' => $this->id));
      $fetcher = $config[$this->id]->config['fetcher']['plugin_key'];
      if ($record = db_query("SELECT imported, config, state, fetcher_result FROM {feeds_source} WHERE id = :id AND feed_nid = :nid", array(':id' => $this->id, ':nid' => $nid))->fetchObject()) {
        $source_config = unserialize($record->config);
        $file_source = $source_config[$fetcher]['source'];
        if ($fetcher == 'FeedsHTTPFetcher') {
          feeds_include_library('http_request.inc', 'http_request');
          $result = http_request_get($file_source, NULL, NULL, NULL, NULL);
          $lines = preg_split('/\r\n|\r|\n/', $result->data, 3);
          $headers = $lines[0];
          $first_row = $lines[1];
          $rows = array();
          $header = array(
            t('Data name'),
            t('Data type'),
          );
          $data_names = explode(',', $headers);
          foreach ($data_names as $name) {
            $rows[] = array(
              $name,
              't',
            );
          }
          $output = theme('table', array('header' => $header, 'rows' => $rows));
        }
        else {
        }
      }
    }
    else {
    }

    $form['help'] = array(
      '#prefix' => '<div class="help">',
      '#suffix' => '</div>',
      'description' => array(
        '#prefix' => '<p>',
        '#markup' => 'wtf',
        '#suffix' => '</p>',
      ),
    );
    return $form;
  }

}
