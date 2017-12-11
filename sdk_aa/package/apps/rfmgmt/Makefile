include $(TOPDIR)/rules.mk

PKG_NAME:=rfmgmt
PKG_VERSION:=01.00

TARGET_CFLAGS += -g -Wall -fexceptions -D_GNU_SOURCE

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
   SECTION:=apps
   CATEGORY:=apps
   TITLE:=RF Management
   DEPENDS:=+libpthread +librt +libuci
endef

define Package/$(PKG_NAME)/description
	RF Management
endef

define Package/$(PKG_NAME)/conffiles
/etc/config/$(PKG_NAME)
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/$(PKG_NAME) $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/getepc $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) ./files/mount_sd $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) ./files/check-rfmgmt.sh $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/database/
	$(INSTALL_BIN) ./files/epcdata.db $(1)/etc/database/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/rfmgmt.init $(1)/etc/init.d/$(PKG_NAME)
	$(INSTALL_DIR) $(1)/etc/config/
	$(INSTALL_DATA) ./files/rfmgmt.config $(1)/etc/config/$(PKG_NAME)
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/ \
	CC="$(TARGET_CC)" \
	CFLAGS="$(TARGET_CFLAGS)"
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
